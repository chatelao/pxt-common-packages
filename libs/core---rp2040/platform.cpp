#include "pxt.h"

#include "RP2040LowLevelTimer.h"

#include "hardware/pll.h"
#include "hardware/clocks.h"
#include "hardware/structs/rosc.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "hardware/irq.h"
#include "pico/multicore.h"
#include "pico/time.h"

namespace pxt {

extern ThreadContext *secondaryThreadCtx[PXT_MAX_CORES];

// ---------------------------------------------------------------------------
// Hardware spinlock & Stop-The-World (STW) GC / Flash coordination
// ---------------------------------------------------------------------------
static spin_lock_t *gcSpinLock;
static volatile int gcLockOwner = -1;
static volatile int gcLockDepth = 0;

static volatile bool core1Launched = false;
static volatile bool core1Active = false;
static volatile bool stwPauseRequested[PXT_MAX_CORES] = {false, false};
static volatile bool stwCoreParked[PXT_MAX_CORES] = {false, false};

#define STW_PAUSE_CMD 0x50415553u // 'PAUS'

int coreId() {
    return (int)sio_hw->cpuid;
}

int coreCount() {
    return 2;
}

static void __attribute__((section(".time_critical.pxt_stw_park")))
stwParkWithSP(void *sp) {
    int cid = (int)sio_hw->cpuid;
    ThreadContext *ctx = (cid == 0) ? (currentFiber ? (ThreadContext *)currentFiber->user_data : NULL)
                                    : secondaryThreadCtx[cid];
    void *prevTop = NULL;
    if (ctx) {
        prevTop = ctx->stack.top;
        ctx->stack.top = sp;
    }
    __dmb();
    stwCoreParked[cid] = true;
    __sev();
    while (stwPauseRequested[cid]) {
        __wfe();
    }
    if (ctx) {
        ctx->stack.top = prevTop;
    }
    __dmb();
    stwCoreParked[cid] = false;
    __sev();
}

static void __attribute__((naked, section(".time_critical.pxt_stw_park_asm")))
stwParkCurrentCore() {
    __asm__ volatile(
        "push {r4-r7, lr}\n"
        "mov r4, r8\n"
        "mov r5, r9\n"
        "mov r6, r10\n"
        "mov r7, r11\n"
        "push {r4-r7}\n"
        "mov r0, sp\n"
        "bl %0\n"
        "pop {r4-r7}\n"
        "mov r8, r4\n"
        "mov r9, r5\n"
        "mov r10, r6\n"
        "mov r11, r7\n"
        "pop {r4-r7, pc}\n"
        :
        : "i"(stwParkWithSP)
        : "r0", "r1", "r2", "r3", "memory");
}

static void __attribute__((section(".time_critical.pxt_sio_irq")))
sioFifoIrqHandler() {
    while (sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS) {
        uint32_t msg = sio_hw->fifo_rd;
        if (msg == STW_PAUSE_CMD) {
            stwParkCurrentCore();
        }
    }
    sio_hw->fifo_st = SIO_FIFO_ST_ROE_BITS | SIO_FIFO_ST_WOF_BITS;
}

void gcLock() {
    if (!gcSpinLock) {
        int num = spin_lock_claim_unused(true);
        gcSpinLock = spin_lock_instance(num);
    }
    int cid = (int)sio_hw->cpuid;
    if (gcLockOwner == cid) {
        gcLockDepth++;
        return;
    }
    // Spin with IRQs enabled so STW FIFO IRQs can still be serviced immediately
    while (*gcSpinLock == 0) {
        if (stwPauseRequested[cid]) {
            stwParkCurrentCore();
        }
    }
    __dmb();
    gcLockOwner = cid;
    gcLockDepth = 1;
}

void gcUnlock() {
    int cid = (int)sio_hw->cpuid;
    if (gcLockOwner != cid)
        return;
    if (--gcLockDepth == 0) {
        gcLockOwner = -1;
        __dmb();
        *gcSpinLock = 1;
    }
}

void gcSuspendOtherCores() {
    if (!core1Active)
        return;
    int cid = (int)sio_hw->cpuid;
    int other = 1 - cid;
    stwCoreParked[other] = false;
    stwPauseRequested[other] = true;
    __dmb();
    // Wake other core via SIO FIFO IRQ
    while (!(sio_hw->fifo_st & SIO_FIFO_ST_RDY_BITS)) {
        tight_loop_contents();
    }
    sio_hw->fifo_wr = STW_PAUSE_CMD;
    __sev();
    while (!stwCoreParked[other]) {
        tight_loop_contents();
    }
    __dmb();
}

void gcResumeOtherCores() {
    if (!core1Active)
        return;
    int cid = (int)sio_hw->cpuid;
    int other = 1 - cid;
    stwPauseRequested[other] = false;
    __dmb();
    __sev();
    while (stwCoreParked[other]) {
        tight_loop_contents();
    }
}

void coreSleepMs(unsigned ms) {
    sleep_ms((uint32_t)ms);
}

// ---------------------------------------------------------------------------
// Core 1 -> Core 0 Marshaling (Serial & Events)
// ---------------------------------------------------------------------------
#define MARSHAL_SERIAL_BUF_SIZE 512
#define MARSHAL_EVENT_QUEUE_SIZE 32

struct MarshalEvent {
    int src;
    int val;
};

static spin_lock_t *marshalSpinLock;
static char marshalSerialBuf[MARSHAL_SERIAL_BUF_SIZE];
static volatile int marshalSerialHead = 0;
static volatile int marshalSerialTail = 0;
static MarshalEvent marshalEvents[MARSHAL_EVENT_QUEUE_SIZE];
static volatile int marshalEventHead = 0;
static volatile int marshalEventTail = 0;

void marshalSendSerial(const char *data, int len) {
    if (!data || len <= 0 || !marshalSpinLock)
        return;
    uint32_t irq = spin_lock_blocking(marshalSpinLock);
    for (int i = 0; i < len; ++i) {
        int next = (marshalSerialHead + 1) % MARSHAL_SERIAL_BUF_SIZE;
        if (next == marshalSerialTail)
            break; // drop if full
        marshalSerialBuf[marshalSerialHead] = data[i];
        marshalSerialHead = next;
    }
    spin_unlock(marshalSpinLock, irq);
}

void marshalRaiseEvent(int src, int val) {
    if (!marshalSpinLock)
        return;
    uint32_t irq = spin_lock_blocking(marshalSpinLock);
    int next = (marshalEventHead + 1) % MARSHAL_EVENT_QUEUE_SIZE;
    if (next != marshalEventTail) {
        marshalEvents[marshalEventHead].src = src;
        marshalEvents[marshalEventHead].val = val;
        marshalEventHead = next;
    }
    spin_unlock(marshalSpinLock, irq);
}

class MulticoreMarshalComponent : public CodalComponent {
  public:
    MulticoreMarshalComponent() {
        status |= DEVICE_COMPONENT_STATUS_SYSTEM_TICK;
    }
    void periodicCallback() override {
        if (sio_hw->cpuid != 0 || !marshalSpinLock)
            return;

        // Drain serial buffer
        char tmp[64];
        int count = 0;
        uint32_t irq = spin_lock_blocking(marshalSpinLock);
        while (marshalSerialTail != marshalSerialHead && count < (int)sizeof(tmp)) {
            tmp[count++] = marshalSerialBuf[marshalSerialTail];
            marshalSerialTail = (marshalSerialTail + 1) % MARSHAL_SERIAL_BUF_SIZE;
        }
        spin_unlock(marshalSpinLock, irq);
        if (count > 0) {
            sendSerial(tmp, count);
        }

        // Drain events
        for (;;) {
            MarshalEvent ev;
            bool hasEv = false;
            irq = spin_lock_blocking(marshalSpinLock);
            if (marshalEventTail != marshalEventHead) {
                ev = marshalEvents[marshalEventTail];
                marshalEventTail = (marshalEventTail + 1) % MARSHAL_EVENT_QUEUE_SIZE;
                hasEv = true;
            }
            spin_unlock(marshalSpinLock, irq);
            if (!hasEv)
                break;
            Event(ev.src, ev.val);
        }
    }
};

// ---------------------------------------------------------------------------
// Core 1 Action Registration & Runner
// ---------------------------------------------------------------------------
#define MAX_CORE_ACTIONS 16
static Action core1StartActions[MAX_CORE_ACTIONS];
static int numCore1StartActions = 0;
static Action core1ForeverActions[MAX_CORE_ACTIONS];
static int numCore1ForeverActions = 0;
static uint32_t core1Stack[1024]; // 4 KB dedicated hardware stack

void runOnStartOnCore(int core, Action a) {
    if (a == 0)
        return;
    if (core != 1) {
        runInParallel(a);
        return;
    }
    registerGCPtr(a);
    if (numCore1StartActions < MAX_CORE_ACTIONS) {
        core1StartActions[numCore1StartActions++] = a;
    }
}

void runForeverOnCore(int core, Action a) {
    if (a == 0)
        return;
    if (core != 1) {
        runForever(a);
        return;
    }
    registerGCPtr(a);
    if (numCore1ForeverActions < MAX_CORE_ACTIONS) {
        core1ForeverActions[numCore1ForeverActions++] = a;
    }
}

static void core1Entry() {
    irq_set_exclusive_handler(SIO_IRQ_PROC1, sioFifoIrqHandler);
    irq_set_enabled(SIO_IRQ_PROC1, true);
    sio_hw->fifo_st = SIO_FIFO_ST_ROE_BITS | SIO_FIFO_ST_WOF_BITS;

    core1Active = true;
    __dmb();

    for (int i = 0; i < numCore1StartActions; ++i) {
        runAction0(core1StartActions[i]);
    }

    while (true) {
        for (int i = 0; i < numCore1ForeverActions; ++i) {
            runAction0(core1ForeverActions[i]);
        }
        coreSleepMs(20);
    }
}

void startSecondaryCores() {
    if (core1Launched)
        return;
    if (numCore1StartActions == 0 && numCore1ForeverActions == 0)
        return;
    core1Launched = true;
    multicore_launch_core1_with_stack(core1Entry, core1Stack, sizeof(core1Stack));
    while (!core1Active) {
        tight_loop_contents();
    }
}

// ---------------------------------------------------------------------------
// Platform Initialization & Existing RP2040 Helpers
// ---------------------------------------------------------------------------
LowLevelTimer *allocateTimer() {
    // TODO: add config to low level timer
    return new RP2040LowLevelTimer();
}

static uint32_t hw_random(void) {
    uint8_t buf[16];
    for (unsigned i = 0; i < sizeof(buf); ++i) {
        buf[i] = 0;
        for (int j = 0; j < 8; ++j) {
            buf[i] <<= 1;
            if (rosc_hw->randombit)
                buf[i] |= 1;
        }
    }
    return hash_fnv1(buf, sizeof(buf));
}

static void initRandomSeed() {
    seedRandom(hw_random());
}

void deepSleep() {}

void platform_init() {
    initRandomSeed();
    if (!gcSpinLock) {
        gcSpinLock = spin_lock_instance(spin_lock_claim_unused(true));
    }
    if (!marshalSpinLock) {
        marshalSpinLock = spin_lock_instance(spin_lock_claim_unused(true));
    }
    irq_set_exclusive_handler(SIO_IRQ_PROC0, sioFifoIrqHandler);
    irq_set_enabled(SIO_IRQ_PROC0, true);
    new MulticoreMarshalComponent();
}

int *getBootloaderConfigData() {
    static bool inited;
    static int *cached;

    if (!inited) {
        inited = 1;
        for (int i = 1; i <= 64; i *= 2) {
            uint32_t *p = (uint32_t *)(XIP_BASE + i * 1024 * 1024 - 4096);
            if (p[0] == CFG_MAGIC0 && p[1] == CFG_MAGIC1) {
                cached = (int *)p;
                break;
            }
        }
    }

    return cached;
}

} // namespace pxt

void cpu_clock_init() {
    // missing in Codal
}

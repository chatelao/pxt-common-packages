#ifndef __PXT_H
#define __PXT_H

#include "pxtbase.h"
#include "vm.h"
#include "pins.h"
#include "pintarget.h"

#define OUTPUT_BITS 12

#define DEVICE_EVT_ANY 0
#define DEVICE_ID_NOTIFY_ONE 1022
#define DEVICE_ID_NOTIFY 1023

#define CHK(call)                                                                                  \
    {                                                                                              \
        int __r = call;                                                                            \
        if (__r != 0) {                                                                            \
            DMESG("fail: %d at %d", __r, __LINE__);                                                \
            abort();                                                                               \
        }                                                                                          \
    }

namespace pxt {
void raiseEvent(int id, int event);
int allocateNotifyEvent();
void sleep_core_us(uint64_t us);

#define DEVICE_ID_FIRST_BUTTON 1000
#define DEVICE_ID_BUTTON_AB 3
#define DEVICE_BUTTON_ALL_EVENTS 1
#define DEVICE_BUTTON_SIMPLE_EVENTS 2
#define DEVICE_BUTTON_EVT_DOWN 1
#define DEVICE_BUTTON_EVT_UP 2
#define DEVICE_BUTTON_EVT_CLICK 3
#define DEVICE_BUTTON_EVT_LONG_CLICK 4

enum ButtonPolarity {
    ACTIVE_LOW = 0,
    ACTIVE_HIGH = 1
};

class CodalComponent {
  public:
    int id;
    CodalComponent(int _id = 0) : id(_id) {}
    virtual ~CodalComponent() {}
};

CodalComponent *lookupComponent(int id);

class AbstractButton : public CodalComponent {
  public:
    AbstractButton(int _id = 0) : CodalComponent(_id) {}
    virtual bool isPressed() = 0;
    virtual bool wasPressed() = 0;
    virtual void setEventConfiguration(int) {}
};

class Button : public AbstractButton {
    ZPin *pin;
    bool activeLow;
    bool lastState;

  public:
    Button(ZPin &p, int _id, int eventConfig, ButtonPolarity polarity, PullMode pull);
    bool isPressed() override;
    bool wasPressed() override;
};

class MultiButton : public AbstractButton {
    int idA;
    int idB;
    bool lastState;

  public:
    MultiButton(int _idA, int _idB, int _id);
    bool isPressed() override;
    bool wasPressed() override;
};

typedef Button *Button_;

} // namespace pxt

namespace pins {
class EspI2CProxy;
class EspSPIProxy;
} // namespace pins

namespace pxt {
typedef ::pins::EspI2CProxy *I2C_;
typedef ::pins::EspSPIProxy *SPI_;

static inline Button_ asButton_(TValue v) { return (Button_)v; }
static inline I2C_ asI2C_(TValue v) { return (I2C_)v; }
static inline SPI_ asSPI_(TValue v) { return (SPI_)v; }

extern volatile bool paniced;
extern char **initialArgv;
void target_exit();
extern volatile int panicCode;

// Buffer, Sound, and Image share representation.
typedef Buffer Sound;


typedef struct worker *worker_t;
worker_t worker_alloc(const char *id, uint32_t stack_size);
int worker_run(worker_t w, TaskFunction_t fn, void *arg);
int worker_run_wait(worker_t w, TaskFunction_t fn, void *arg);
void worker_set_idle(worker_t w, TaskFunction_t fn, void *arg);
extern worker_t fg_worker;

void memInfo();

extern void (*logJDFrame)(const uint8_t *data);
extern void (*sendJDFrame)(const uint8_t *data);

} // namespace pxt

extern "C" void target_disable_irq();
extern "C" void target_enable_irq();
extern "C" void target_init();
extern "C" void target_wait_us(uint32_t us);

#define DEVICE_ID_JACDAC 30
#define DEVICE_ID_JACDAC_PHYS 31

#undef PXT_MAIN
#define PXT_MAIN                                                                                   \
    extern "C" int app_main() {                                                              \
        pxt::vmStart();                                                                            \
        return 0;                                                                                  \
    }

#undef PXT_SHIMS_BEGIN
#define PXT_SHIMS_BEGIN                                                                            \
    namespace pxt {                                                                                \
    const OpcodeDesc staticOpcodes[] __attribute__((aligned(0x20))) = {

#undef PXT_SHIMS_END
#define PXT_SHIMS_END                                                                              \
    { 0, 0, 0 }                                                                                    \
    }                                                                                              \
    ;                                                                                              \
    }

#endif

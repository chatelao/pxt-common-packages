#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <vector>
#include <string>

#define PXT_ESP32 1
#define CONFIG_IDF_TARGET_ESP32C6 1
#define ESP_IDF_VERSION_VAL(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(5, 5, 6)

#define CFG_PIN_WIFI_RESET 89
#define CFG_PIN_WIFI_GPIO0 90

#define DEVICE_ID_RADIO 29
#define DEVICE_RADIO_EVT_DATAGRAM 1
#define DEVICE_RADIO_MAX_PACKET_SIZE 32

typedef int32_t TValue;
typedef void *Action;
typedef void (*TaskFunction_t)(void *);

struct BoxedBuffer {
    uint32_t length;
    uint8_t data[1];
};
typedef BoxedBuffer *Buffer;

struct BoxedString {
    std::string str;
    const char *getUTF8Data() const { return str.c_str(); }
};
typedef BoxedString *String;

struct FiberContext {
    int id;
};

#define PXT_DEF_STRING(name, val) static const char *name = val;

namespace pxt {
typedef struct worker *worker_t;
typedef Buffer (*fiber_resume_t)(void *);
extern void raiseEvent(int id, int event);
extern void registerWithDal(int id, int event, Action a, int flags = 0);
extern Buffer mkBuffer(const void *data, int len);
extern void freeBuffer(Buffer b);
extern String mkString(const char *s, int len = -1);
extern TValue fromInt(int v);
extern int getConfig(int key, int defl);
extern void memInfo();
extern void vm_stack_trace();

extern worker_t worker_alloc(const char *id, uint32_t stack_size);
extern void worker_set_idle(worker_t w, TaskFunction_t fn, void *arg);
extern int worker_run(worker_t w, TaskFunction_t fn, void *arg);

extern FiberContext *suspendFiber();
extern void resumeFiber(FiberContext *ctx, TValue v);
extern void resumeFiberWithFn(FiberContext *ctx, fiber_resume_t fn, void *arg);
} // namespace pxt

extern "C" void target_panic(int error_code);

using namespace pxt;

#define DMESG(...) ((void)0)

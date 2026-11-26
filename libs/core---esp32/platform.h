#ifndef __PXT_PLATFORM_H
#define __PXT_PLATFORM_H

#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PXT_ESP32 1

#if CONFIG_IDF_TARGET_ESP32S2
#define PXT_USB 1
#else
#define PXT_USB 0
#endif

#define PAGE_SIZE 1024 // not really

#define DEV_NUM_PINS 50

#define DEV_PWM_PINS 0xffff_ffff_ffffULL
#if CONFIG_IDF_TARGET_ESP32C6
#define DEV_AIN_PINS 0x7fULL // GPIO0..GPIO6 (ADC1)
#else
#define DEV_AIN_PINS 0ULL
#endif

// Codal doesn't yet distinguish between PWM and AIN
#define DEV_ANALOG_PINS (DEV_PWM_PINS | DEV_AIN_PINS)

namespace pxt {
class ZPin;
class AbstractButton;
class MultiButton;
class Button;
class CodalComponent;

typedef int8_t PinName;

typedef void (*reset_fn_t)();
void registerResetFunction(reset_fn_t fn);
void soft_panic(int errorCode);

extern TaskHandle_t userCodeTask;

void install_gpio0_handler();

} // namespace pxt

#define IMAGE_BITS 4

#define PXT_IN_ISR() (xTaskGetCurrentTaskHandle() != userCodeTask)

#define GC_BLOCK_SIZE (1024 * 16)

#define PXT_REGISTER_RESET(fn) pxt::registerResetFunction(fn)

#if CONFIG_IDF_TARGET_ESP32C6
// 0x42000000-0x43000000 unified MMU flash cache on ESP32-C6
#define PXT_IS_READONLY(v) (isTagged(v) || ((uintptr_t)(v) >> 24) == 0x42)
#elif CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3
#define PXT_IS_READONLY(v) (isTagged(v) || ((uintptr_t)(v) >> 24) == 0x3c)
#elif CONFIG_IDF_TARGET_ESP32S2
// 0x3f000000-... range of data SPI flash (we only support first 4M)
#define PXT_IS_READONLY(v) (isTagged(v) || ((uintptr_t)(v) >> 22) == 0xfc)
#else
// 0x3f400000-0x3f700000 range of data SPI flash
#define PXT_IS_READONLY(v) (isTagged(v) || ((uintptr_t)(v) >> 22) == 0xfd)
#endif


#define CODAL_PIN ::pxt::ZPin

extern "C" {
extern const uintptr_t PXT_EXPORTData[];
}

#if CONFIG_FREERTOS_UNICORE || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32C3
#define WORKER_CPU PRO_CPU_NUM
#else
#define WORKER_CPU APP_CPU_NUM
#endif

#endif

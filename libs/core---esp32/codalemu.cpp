#include "pxt.h"

IRAM_ATTR void target_wait_us(uint32_t us) {
    int64_t later = esp_timer_get_time() + us;
    while (esp_timer_get_time() < later) {
        ;
    }
}

#if !CONFIG_FREERTOS_UNICORE
static portMUX_TYPE global_int_mux = portMUX_INITIALIZER_UNLOCKED;
#endif
int int_level;

IRAM_ATTR void target_disable_irq() {
#if CONFIG_FREERTOS_UNICORE
    vPortEnterCritical();
#else
    vPortEnterCritical(&global_int_mux);
#endif
    int_level++;
}

IRAM_ATTR void target_enable_irq() {
    int_level--;
#if CONFIG_FREERTOS_UNICORE
    vPortExitCritical();
#else
    vPortExitCritical(&global_int_mux);
#endif
}

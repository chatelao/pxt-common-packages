#include "pxt.h"

#include "esp_system.h"
#include "esp_random.h"
#include "esp_log.h"
#include "rom/ets_sys.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_private/system_internal.h"

static void reset_pin(PinName p) {
    if (p != -1)
        gpio_set_direction((gpio_num_t)p, GPIO_MODE_DISABLE);
}

void reboot_to_uf2(void) {
    reset_pin(PIN(LED_R));
    reset_pin(PIN(LED_G));
    reset_pin(PIN(LED_B));

#if CONFIG_IDF_TARGET_ESP32S2
    // call esp_reset_reason() is required for idf.py to properly links esp_reset_reason_set_hint()
    (void)esp_reset_reason();
    esp_reset_reason_set_hint((esp_reset_reason_t)0x11F2);
#endif

    esp_restart();
}

namespace pxt {

static void initRandomSeed() {
    seedRandom(esp_random());
}

extern "C" void drawPanic(int code) {
    // TODO
}

#if CONFIG_IDF_TARGET_ESP32S2
static void bootloader_handler(void *) {
    reboot_to_uf2();
}

void install_gpio0_handler() {
    DMESG("reset reason: %x", esp_reset_reason_get_hint());
    gpio_install_isr_service(0);
    gpio_config_t gpio_conf;
    gpio_conf.intr_type = GPIO_INTR_POSEDGE;
    gpio_conf.mode = GPIO_MODE_INPUT;
    gpio_conf.pin_bit_mask = (1ULL << GPIO_NUM_0);
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&gpio_conf);
    gpio_isr_handler_add(GPIO_NUM_0, bootloader_handler, NULL);
}
#else
void install_gpio0_handler() {
    gpio_install_isr_service(0);
}
#endif

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
static void usb_serial_send(const char *data, int len) {
    if (data && len > 0) {
        if (usb_serial_jtag_is_connected()) {
            usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(10));
        } else {
            for (int i = 0; i < len; i++)
                ets_printf("%c", data[i]);
        }
    }
}
#endif

extern "C" void usb_init();
extern "C" void target_init() {
    initRandomSeed();
    memInfo();
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t jtag_cfg = {
        .tx_buffer_size = 512,
        .rx_buffer_size = 512,
    };
    usb_serial_jtag_driver_install(&jtag_cfg);
    setSendToUART(usb_serial_send);
#endif
#if CONFIG_IDF_TARGET_ESP32C6
    int rfEn = getConfig(CFG_PIN_WIFI_RESET, -1);
    int rfAnt = getConfig(CFG_PIN_WIFI_GPIO0, -1);
    if (rfEn >= 0) {
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = (1ULL << rfEn);
        cfg.mode = GPIO_MODE_OUTPUT;
        gpio_config(&cfg);
        gpio_set_level((gpio_num_t)rfEn, 0);
    }
    if (rfAnt >= 0) {
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = (1ULL << rfAnt);
        cfg.mode = GPIO_MODE_OUTPUT;
        gpio_config(&cfg);
        gpio_set_level((gpio_num_t)rfAnt, 0);
    }
#endif
    usb_init();
}

void updateScreen(Image_ img);

void screen_init() {
    updateScreen(NULL);
}

void memInfo() {
    // heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
    DMESG("mem: %d free (%d total)", heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
          heap_caps_get_total_size(MALLOC_CAP_DEFAULT));
}

void (*logJDFrame)(const uint8_t *data);
void (*sendJDFrame)(const uint8_t *data);

} // namespace pxt

extern "C" int pxt_802154_claim(int mode) {
    static int claimed = 0; // 0 = unclaimed, 1 = OpenThread, 2 = Zigbee
    if (claimed == 0)
        claimed = mode;
    return claimed == mode ? 0 : -1;
}


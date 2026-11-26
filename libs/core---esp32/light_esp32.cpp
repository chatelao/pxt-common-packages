#include "pxt.h"
#include "esp_idf_version.h"
#include "driver/gpio.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "driver/rmt_tx.h"
#endif

#define LIGHTMODE_DOTSTAR 4

namespace light {

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static rmt_channel_handle_t s_rmt_chan = NULL;
static rmt_encoder_handle_t s_rmt_enc = NULL;
static int s_rmt_pin = -1;

static void ensure_rmt_channel(int gpio_num) {
    if (s_rmt_chan && s_rmt_pin == gpio_num) {
        return;
    }
    if (s_rmt_chan) {
        rmt_disable(s_rmt_chan);
        rmt_del_channel(s_rmt_chan);
        s_rmt_chan = NULL;
        s_rmt_pin = -1;
    }
    if (!s_rmt_enc) {
        // WS2812B timings at 10 MHz (1 tick = 100 ns):
        // 0: 300ns high, 900ns low; 1: 900ns high, 300ns low
        rmt_bytes_encoder_config_t enc_cfg = {};
        enc_cfg.bit0.level0 = 1;
        enc_cfg.bit0.duration0 = 3;
        enc_cfg.bit0.level1 = 0;
        enc_cfg.bit0.duration1 = 9;
        enc_cfg.bit1.level0 = 1;
        enc_cfg.bit1.duration0 = 9;
        enc_cfg.bit1.level1 = 0;
        enc_cfg.bit1.duration1 = 3;
        enc_cfg.flags.msb_first = 1;
        if (rmt_new_bytes_encoder(&enc_cfg, &s_rmt_enc) != ESP_OK) {
            s_rmt_enc = NULL;
            return;
        }
    }
    rmt_tx_channel_config_t tx_cfg = {};
    tx_cfg.gpio_num = (gpio_num_t)gpio_num;
    tx_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_cfg.resolution_hz = 10 * 1000 * 1000; // 10 MHz
    tx_cfg.mem_block_symbols = 48;
    tx_cfg.trans_queue_depth = 4;
    if (rmt_new_tx_channel(&tx_cfg, &s_rmt_chan) == ESP_OK) {
        if (rmt_enable(s_rmt_chan) == ESP_OK) {
            s_rmt_pin = gpio_num;
        } else {
            rmt_del_channel(s_rmt_chan);
            s_rmt_chan = NULL;
        }
    } else {
        s_rmt_chan = NULL;
    }
}
#endif

//%
void sendBuffer(DigitalInOutPin data, DigitalInOutPin clk, int mode, Buffer buf) {
    if (!data || !buf || !buf->length) {
        return;
    }
    int dataPin = data->getGpioNum();
    if (dataPin < 0) {
        return;
    }

    if ((mode & 0xff) == LIGHTMODE_DOTSTAR && clk) {
        int clkPin = clk->getGpioNum();
        if (clkPin < 0)
            return;
        gpio_set_direction((gpio_num_t)dataPin, GPIO_MODE_OUTPUT);
        gpio_set_direction((gpio_num_t)clkPin, GPIO_MODE_OUTPUT);
        for (int i = 0; i < buf->length; i++) {
            uint8_t b = buf->data[i];
            for (int bit = 7; bit >= 0; bit--) {
                gpio_set_level((gpio_num_t)dataPin, (b >> bit) & 1);
                gpio_set_level((gpio_num_t)clkPin, 1);
                gpio_set_level((gpio_num_t)clkPin, 0);
            }
        }
        return;
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    ensure_rmt_channel(dataPin);
    if (s_rmt_chan && s_rmt_enc) {
        rmt_transmit_config_t tx_conf = {};
        tx_conf.loop_count = 0;
        if (rmt_transmit(s_rmt_chan, s_rmt_enc, buf->data, buf->length, &tx_conf) == ESP_OK) {
            // Bounded wait so unemulated RMT in QEMU cannot deadlock
            rmt_tx_wait_all_done(s_rmt_chan, pdMS_TO_TICKS(50));
        }
    }
#endif
}

} // namespace light

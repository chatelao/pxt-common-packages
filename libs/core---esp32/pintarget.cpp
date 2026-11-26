#include "pxt.h"

#include "esp_system.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "esp_adc/adc_oneshot.h"
#endif

#define IO_STATUS_DIGITAL_IN 0x0001          // Pin is configured as a digital input, with no pull up.
#define IO_STATUS_DIGITAL_OUT 0x0002         // Pin is configured as a digital output
#define IO_STATUS_ANALOG_IN 0x0004           // Pin is Analog in
#define IO_STATUS_ANALOG_OUT 0x0008          // Pin is Analog out
#define IO_STATUS_TOUCH_IN 0x0010            // Pin is a makey-makey style touch sensor
#define IO_STATUS_EVENT_ON_EDGE 0x0020       // Pin will generate events on pin change
#define IO_STATUS_EVENT_PULSE_ON_EDGE 0x0040 // Pin will generate events on pin change
#define IO_STATUS_INTERRUPT_ON_EDGE 0x0080   // Pin will generate events on pin change

namespace pxt {

PXT_EXT_VTABLE(ZPin);

#if CONFIG_IDF_TARGET_ESP32C6
#define PXT_LEDC_MODE LEDC_LOW_SPEED_MODE
#define PXT_LEDC_MAX_CH 6
#else
#define PXT_LEDC_MODE LEDC_LOW_SPEED_MODE
#define PXT_LEDC_MAX_CH 8
#endif

static ZPin *ledc_owners[PXT_LEDC_MAX_CH] = {0};

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static bool adc1_chan_init[8] = {false};

static int gpio_to_adc1_channel(int gpio) {
#if CONFIG_IDF_TARGET_ESP32C6
    if (gpio >= 0 && gpio <= 6)
        return gpio; // GPIO0..GPIO6 -> ADC_CHANNEL_0..ADC_CHANNEL_6
#endif
    return -1;
}
#endif

static void IRAM_ATTR zpin_gpio_isr(void *arg) {
    ZPin *pin = (ZPin *)arg;
    int level = gpio_get_level((gpio_num_t)pin->getGpioNum());
    uint64_t now = (uint64_t)esp_timer_get_time();
    if (pin->id) {
        raiseEvent(pin->id, level ? DEVICE_PIN_EVT_RISE : DEVICE_PIN_EVT_FALL);
        // Pulse events carry the opposite transition (pulse ends on opposite edge)
        raiseEvent(pin->id, level ? DEVICE_PIN_EVT_PULSE_LO : DEVICE_PIN_EVT_PULSE_HI);
    }
    (void)now;
}

ZPin::ZPin(int gpio_num) : RefObject(&ZPin_vtable) {
    this->gpio_num = gpio_num;
    this->status = 0;
    this->pullMode = PullMode::None;
    this->ledc_channel = -1;
    this->pwm_period_us = 20000;
    this->last_edge_us = 0;
    this->id = 100 + gpio_num;
    this->name = gpio_num;
}

void ZPin::disconnect() {
    if (status & (IO_STATUS_EVENT_ON_EDGE | IO_STATUS_EVENT_PULSE_ON_EDGE | IO_STATUS_INTERRUPT_ON_EDGE)) {
        gpio_isr_handler_remove((gpio_num_t)gpio_num);
        gpio_set_intr_type((gpio_num_t)gpio_num, GPIO_INTR_DISABLE);
    }
    if (ledc_channel >= 0 && ledc_channel < PXT_LEDC_MAX_CH) {
        ledc_stop(PXT_LEDC_MODE, (ledc_channel_t)ledc_channel, 0);
        if (ledc_owners[ledc_channel] == this)
            ledc_owners[ledc_channel] = NULL;
        ledc_channel = -1;
    }
    gpio_reset_pin((gpio_num_t)gpio_num);
    status = 0;
}

int ZPin::setDigitalValue(int value) {
    // Write the value, before setting as output - this way the pin state update will be atomic
    gpio_set_level((gpio_num_t)gpio_num, value);

    // Move into a Digital output state if necessary.
    if (!(status & IO_STATUS_DIGITAL_OUT)) {
        disconnect();

        gpio_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pin_bit_mask = 1ULL << gpio_num;
        cfg.mode = GPIO_MODE_OUTPUT;
        gpio_config(&cfg);
        gpio_set_level((gpio_num_t)gpio_num, value);

        status |= IO_STATUS_DIGITAL_OUT;
    }

    return 0;
}

int ZPin::getDigitalValue() {
    if (!(status & (IO_STATUS_DIGITAL_IN | IO_STATUS_EVENT_ON_EDGE | IO_STATUS_EVENT_PULSE_ON_EDGE |
                    IO_STATUS_INTERRUPT_ON_EDGE))) {
        disconnect();
        gpio_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pin_bit_mask = 1ULL << gpio_num;
        cfg.mode = GPIO_MODE_INPUT;
        switch (pullMode) {
        case PullMode::Down:
            cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
            break;
        case PullMode::Up:
            cfg.pull_up_en = GPIO_PULLUP_ENABLE;
            break;
        case PullMode::None:
            break;
        }
        gpio_config(&cfg);

        status |= IO_STATUS_DIGITAL_IN;
    }

    return gpio_get_level((gpio_num_t)gpio_num);
}

int ZPin::setPull(PullMode pull) {
    if (pull != pullMode) {
        status = 0;
        pullMode = pull;
        getDigitalValue();
    }
    return 0;
}

int ZPin::getDigitalValue(PullMode pull) {
    setPull(pull);
    return getDigitalValue();
}

int ZPin::eventOn(int eventType) {
    switch (eventType) {
    case DEVICE_PIN_EVENT_ON_EDGE:
    case DEVICE_PIN_EVENT_ON_PULSE: {
        getDigitalValue();
        gpio_set_intr_type((gpio_num_t)gpio_num, GPIO_INTR_ANYEDGE);
        gpio_isr_handler_remove((gpio_num_t)gpio_num);
        gpio_isr_handler_add((gpio_num_t)gpio_num, zpin_gpio_isr, this);
        status |= (eventType == DEVICE_PIN_EVENT_ON_PULSE) ? IO_STATUS_EVENT_PULSE_ON_EDGE : IO_STATUS_EVENT_ON_EDGE;
        break;
    }
    case DEVICE_PIN_EVENT_ON_TOUCH:
        break;
    case DEVICE_PIN_EVENT_NONE:
        gpio_isr_handler_remove((gpio_num_t)gpio_num);
        gpio_set_intr_type((gpio_num_t)gpio_num, GPIO_INTR_DISABLE);
        status &= ~(IO_STATUS_EVENT_ON_EDGE | IO_STATUS_EVENT_PULSE_ON_EDGE | IO_STATUS_INTERRUPT_ON_EDGE);
        break;
    default:
        return -1;
    }

    return 0;
}

int ZPin::getAnalogValue() {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    int ch = gpio_to_adc1_channel(gpio_num);
    if (ch < 0)
        return 0;
    if (!(status & IO_STATUS_ANALOG_IN)) {
        disconnect();
        if (!adc1_handle) {
            adc_oneshot_unit_init_cfg_t init_cfg = {
                .unit_id = ADC_UNIT_1,
#if CONFIG_IDF_TARGET_ESP32C6
                .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
#else
                .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
#endif
                .ulp_mode = ADC_ULP_MODE_DISABLE,
            };
            if (adc_oneshot_new_unit(&init_cfg, &adc1_handle) != ESP_OK)
                return 0;
        }
        if (!adc1_chan_init[ch]) {
            adc_oneshot_chan_cfg_t chan_cfg = {
                .atten = ADC_ATTEN_DB_12,
                .bitwidth = ADC_BITWIDTH_12,
            };
            adc_oneshot_config_channel(adc1_handle, (adc_channel_t)ch, &chan_cfg);
            adc1_chan_init[ch] = true;
        }
        status |= IO_STATUS_ANALOG_IN;
    }
    int raw = 0;
    if (adc_oneshot_read(adc1_handle, (adc_channel_t)ch, &raw) != ESP_OK)
        return 0;
    if (raw < 0) raw = 0;
    if (raw > 4095) raw = 4095;
    return raw >> 2; // 12-bit -> 0..1023
#else
    return 0;
#endif
}

static void configure_ledc_timer(uint32_t period_us) {
    if (period_us < 10)
        period_us = 10;
    uint32_t freq_hz = 1000000UL / period_us;
    if (freq_hz < 1)
        freq_hz = 1;
    ledc_timer_config_t timer_conf = {};
    timer_conf.speed_mode = PXT_LEDC_MODE;
    timer_conf.duty_resolution = LEDC_TIMER_10_BIT;
    timer_conf.timer_num = LEDC_TIMER_0;
    timer_conf.freq_hz = freq_hz;
    timer_conf.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&timer_conf);
}

int ZPin::setAnalogValue(int value) {
    if (value < 0) value = 0;
    if (value > 1023) value = 1023;

    if (!(status & IO_STATUS_ANALOG_OUT) || ledc_channel < 0) {
        disconnect();
        int ch = -1;
        for (int i = 0; i < PXT_LEDC_MAX_CH; i++) {
            if (!ledc_owners[i]) {
                ch = i;
                break;
            }
        }
        if (ch < 0)
            return -1;
        ledc_owners[ch] = this;
        ledc_channel = ch;
        configure_ledc_timer(pwm_period_us);

        ledc_channel_config_t ch_conf = {};
        ch_conf.gpio_num = gpio_num;
        ch_conf.speed_mode = PXT_LEDC_MODE;
        ch_conf.channel = (ledc_channel_t)ch;
        ch_conf.intr_type = LEDC_INTR_DISABLE;
        ch_conf.timer_sel = LEDC_TIMER_0;
        ch_conf.duty = (uint32_t)value;
        ch_conf.hpoint = 0;
        ledc_channel_config(&ch_conf);
        status |= IO_STATUS_ANALOG_OUT;
    } else {
        ledc_set_duty(PXT_LEDC_MODE, (ledc_channel_t)ledc_channel, (uint32_t)value);
        ledc_update_duty(PXT_LEDC_MODE, (ledc_channel_t)ledc_channel);
    }
    return 0;
}

int ZPin::setAnalogPeriodUs(int period) {
    if (period <= 0)
        return -1;
    pwm_period_us = (uint32_t)period;
    if (status & IO_STATUS_ANALOG_OUT) {
        configure_ledc_timer(pwm_period_us);
    }
    return 0;
}

int ZPin::setServoPulseUs(int duration) {
    if (duration < 0) duration = 0;
    pwm_period_us = 20000;
    int duty = (int)(((int64_t)duration * 1023) / 20000);
    setAnalogPeriodUs(20000);
    return setAnalogValue(duty);
}

int ZPin::setServoValue(int value, int range, int center) {
    if (value < 0) value = 0;
    if (value > 180) value = 180;
    int min_us = center - range / 2;
    int pulse_us = min_us + (value * range) / 180;
    return setServoPulseUs(pulse_us);
}

uint32_t readButtonMultiplexer(int bits) {
    return 0;
}

} // namespace pxt
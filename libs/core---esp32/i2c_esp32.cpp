#include "pxt.h"
#include "esp_idf_version.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
#include "driver/i2c_master.h"
#else
#include "driver/i2c.h"
#endif

namespace pins {

class EspI2CProxy {
    DevicePin *sda;
    DevicePin *scl;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
    i2c_master_bus_handle_t bus;
#endif

  public:
    EspI2CProxy *next;

    EspI2CProxy(DevicePin *_sda, DevicePin *_scl) : sda(_sda), scl(_scl), next(NULL) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
        i2c_master_bus_config_t bus_cfg = {};
        bus_cfg.i2c_port = I2C_NUM_0;
        bus_cfg.sda_io_num = (gpio_num_t)sda->getGpioNum();
        bus_cfg.scl_io_num = (gpio_num_t)scl->getGpioNum();
        bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_cfg.glitch_ignore_cnt = 7;
        bus_cfg.flags.enable_internal_pullup = true;
        i2c_new_master_bus(&bus_cfg, &bus);
#endif
    }

    bool matchPins(DevicePin *s, DevicePin *c) {
        return this->sda == s && this->scl == c;
    }

    Buffer readBuffer(int address, int size, bool repeat = false) {
        Buffer buf = mkBuffer(NULL, size);
        registerGCObj(buf);
        esp_err_t err = ESP_FAIL;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = (uint16_t)address;
        dev_cfg.scl_speed_hz = 100000;
        i2c_master_dev_handle_t dev = NULL;
        if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) == ESP_OK) {
            err = i2c_master_receive(dev, buf->data, size, 1000);
            i2c_master_bus_rm_device(dev);
        }
#endif
        unregisterGCObj(buf);
        if (err != ESP_OK)
            return NULL;
        (void)repeat;
        return buf;
    }

    int writeBuffer(int address, Buffer buf, bool repeat = false) {
        esp_err_t err = ESP_FAIL;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = (uint16_t)address;
        dev_cfg.scl_speed_hz = 100000;
        i2c_master_dev_handle_t dev = NULL;
        if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) == ESP_OK) {
            err = i2c_master_transmit(dev, buf->data, buf->length, 1000);
            i2c_master_bus_rm_device(dev);
        }
#endif
        (void)repeat;
        return err == ESP_OK ? 0 : -1;
    }
};

static EspI2CProxy *i2cs = NULL;

//%
I2C_ createI2C(DigitalInOutPin sda, DigitalInOutPin scl) {
    if (!sda || !scl) {
        sda = LOOKUP_PIN(SDA);
        scl = LOOKUP_PIN(SCL);
    }
    auto dev = i2cs;
    while (dev) {
        if (dev->matchPins(sda, scl))
            return dev;
        dev = dev->next;
    }
    auto n = new EspI2CProxy(sda, scl);
    n->next = i2cs;
    i2cs = n;
    return n;
}

} // namespace pins

namespace I2CMethods {
//%
Buffer readBuffer(I2C_ i2c, int address, int size, bool repeat = false) {
    return i2c->readBuffer(address, size, repeat);
}

//%
int writeBuffer(I2C_ i2c, int address, Buffer buf, bool repeat = false) {
    return i2c->writeBuffer(address, buf, repeat);
}
} // namespace I2CMethods

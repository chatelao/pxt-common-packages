#include "pxt.h"
#include "driver/spi_master.h"

namespace pins {

class EspSPIProxy {
    DevicePin *mosi;
    DevicePin *miso;
    DevicePin *sck;
    spi_device_handle_t handle;
    int freq_hz;
    uint8_t mode;

    void reinit() {
        if (handle) {
            spi_bus_remove_device(handle);
            handle = NULL;
            spi_bus_free(SPI2_HOST);
        }
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = mosi ? mosi->getGpioNum() : -1;
        buscfg.miso_io_num = miso ? miso->getGpioNum() : -1;
        buscfg.sclk_io_num = sck ? sck->getGpioNum() : -1;
        buscfg.quadwp_io_num = -1;
        buscfg.quadhd_io_num = -1;
        spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

        spi_device_interface_config_t devcfg = {};
        devcfg.clock_speed_hz = freq_hz;
        devcfg.mode = mode;
        devcfg.spics_io_num = -1;
        devcfg.queue_size = 1;
        spi_bus_add_device(SPI2_HOST, &devcfg, &handle);
    }

  public:
    EspSPIProxy *next;

    EspSPIProxy(DevicePin *_mosi, DevicePin *_miso, DevicePin *_sck)
        : mosi(_mosi), miso(_miso), sck(_sck), handle(NULL), freq_hz(1000000), mode(0), next(NULL) {
        reinit();
    }

    bool matchPins(DevicePin *m, DevicePin *mi, DevicePin *s) {
        return mosi == m && miso == mi && sck == s;
    }

    int write(int value) {
        uint8_t tx = (uint8_t)value;
        uint8_t rx = 0;
        spi_transaction_t t = {};
        t.length = 8;
        t.tx_buffer = &tx;
        t.rx_buffer = &rx;
        spi_device_polling_transmit(handle, &t);
        return rx;
    }

    void transfer(Buffer command, Buffer response) {
        size_t clen = command ? command->length : 0;
        size_t rlen = response ? response->length : 0;
        size_t len = clen > rlen ? clen : rlen;
        if (len == 0)
            return;
        spi_transaction_t t = {};
        t.length = len * 8;
        t.tx_buffer = command ? command->data : NULL;
        t.rx_buffer = response ? response->data : NULL;
        spi_device_polling_transmit(handle, &t);
    }

    void setFrequency(int frequency) {
        freq_hz = frequency;
        reinit();
    }

    void setMode(int m) {
        mode = (uint8_t)(m & 3);
        reinit();
    }
};

static EspSPIProxy *spis = NULL;

//%
SPI_ createSPI(DigitalInOutPin mosiPin, DigitalInOutPin misoPin, DigitalInOutPin sckPin) {
    auto dev = spis;
    while (dev) {
        if (dev->matchPins(mosiPin, misoPin, sckPin))
            return dev;
        dev = dev->next;
    }
    auto ser = new EspSPIProxy(mosiPin, misoPin, sckPin);
    ser->next = spis;
    spis = ser;
    return ser;
}

//%
SPI_ createSlaveSPI(DigitalInOutPin mosiPin, DigitalInOutPin misoPin, DigitalInOutPin sckPin, DigitalInOutPin csPin) {
    return NULL;
}

} // namespace pins

namespace SPIMethods {
//%
int write(SPI_ device, int value) {
    return device->write(value);
}

//%
void transfer(SPI_ device, Buffer command, Buffer response) {
    device->transfer(command, response);
}

//%
void setFrequency(SPI_ device, int frequency) {
    device->setFrequency(frequency);
}

//%
void setMode(SPI_ device, int mode) {
    device->setMode(mode);
}
} // namespace SPIMethods

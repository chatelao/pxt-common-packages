#include "pxt.h"
#include "serial-common.h"
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#endif

namespace serial {

static EspSerialDevice *serialDevices = NULL;

EspSerialDevice::EspSerialDevice(uint16_t _id, int txPin, int rxPin)
    : id(_id), uartNum(UART_NUM_1), useUsbConsole(txPin < 0 || rxPin < 0), baudRate(115200),
      delim(-1), buffer(NULL), readp(0), writep(0), buffersz(0), next(NULL) {
    lock = xSemaphoreCreateMutex();
    setRxBufferSize(256);
    if (!useUsbConsole) {
        uart_config_t cfg = {};
        cfg.baud_rate = baudRate;
        cfg.data_bits = UART_DATA_8_BITS;
        cfg.parity = UART_PARITY_DISABLE;
        cfg.stop_bits = UART_STOP_BITS_1;
        cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        uart_driver_install(uartNum, 512, 512, 0, NULL, 0);
        uart_param_config(uartNum, &cfg);
        uart_set_pin(uartNum, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    startRxTask();
}

void EspSerialDevice::startRxTask() {
    xTaskCreatePinnedToCore(rxTaskFn, "pxt_ser", 2048, this, 2, NULL, WORKER_CPU);
}

void EspSerialDevice::rxTaskFn(void *arg) {
    ((EspSerialDevice *)arg)->rxLoop();
}

void EspSerialDevice::rxLoop() {
    uint8_t tmp[64];
    while (true) {
        int r = 0;
        if (useUsbConsole) {
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
            r = usb_serial_jtag_read_bytes(tmp, sizeof(tmp), pdMS_TO_TICKS(50));
#else
            vTaskDelay(pdMS_TO_TICKS(50));
#endif
        } else {
            r = uart_read_bytes(uartNum, tmp, sizeof(tmp), pdMS_TO_TICKS(50));
        }
        if (r > 0 && buffer && buffersz > 0) {
            xSemaphoreTake(lock, portMAX_DELAY);
            for (int i = 0; i < r; i++) {
                unsigned nextw = (writep + 1) % buffersz;
                if (nextw != readp) {
                    buffer[writep] = tmp[i];
                    writep = nextw;
                }
            }
            xSemaphoreGive(lock);
            raiseEvent(id, CODAL_SERIAL_EVT_DATA_RECEIVED);
            if (delim != -1) {
                for (int i = 0; i < r; i++) {
                    if (tmp[i] == delim)
                        raiseEvent(id, CODAL_SERIAL_EVT_DELIM_MATCH);
                }
            }
        }
    }
}

int EspSerialDevice::bufferedSize() {
    int amount = (int)writep - (int)readp;
    if (amount < 0)
        amount += buffersz;
    return amount;
}

void EspSerialDevice::setRxBufferSize(unsigned size) {
    if (size < 64)
        size = 64;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (buffer)
        free(buffer);
    buffer = (uint8_t *)malloc(size);
    buffersz = size;
    readp = writep = 0;
    xSemaphoreGive(lock);
}

void EspSerialDevice::setBaudRate(int rate) {
    baudRate = rate;
    if (!useUsbConsole)
        uart_set_baudrate(uartNum, rate);
}

void EspSerialDevice::redirect(DigitalInOutPin tx, DigitalInOutPin rx, BaudRate rate) {
    if (!tx || !rx)
        return;
    if (useUsbConsole) {
        useUsbConsole = false;
        uart_config_t cfg = {};
        cfg.baud_rate = (int)rate;
        cfg.data_bits = UART_DATA_8_BITS;
        cfg.parity = UART_PARITY_DISABLE;
        cfg.stop_bits = UART_STOP_BITS_1;
        cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        uart_driver_install(uartNum, 512, 512, 0, NULL, 0);
        uart_param_config(uartNum, &cfg);
    } else {
        uart_set_baudrate(uartNum, (int)rate);
    }
    uart_set_pin(uartNum, tx->getGpioNum(), rx->getGpioNum(), UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

int EspSerialDevice::read() {
    xSemaphoreTake(lock, portMAX_DELAY);
    int ch = -1;
    if (readp != writep && buffer) {
        ch = buffer[readp];
        readp = (readp + 1) % buffersz;
    }
    xSemaphoreGive(lock);
    return ch;
}

int EspSerialDevice::readBuf(void *dst, int sz) {
    xSemaphoreTake(lock, portMAX_DELAY);
    int avail = bufferedSize();
    if (sz > avail)
        sz = avail;
    uint8_t *out = (uint8_t *)dst;
    for (int i = 0; i < sz; i++) {
        out[i] = buffer[readp];
        readp = (readp + 1) % buffersz;
    }
    xSemaphoreGive(lock);
    return sz;
}

Buffer EspSerialDevice::readBuffer() {
    int sz = bufferedSize();
    if (sz <= 0)
        return mkBuffer(NULL, 0);
    Buffer res = mkBuffer(NULL, sz);
    registerGCObj(res);
    readBuf(res->data, sz);
    unregisterGCObj(res);
    return res;
}

void EspSerialDevice::writeBuffer(Buffer buf) {
    if (!buf || !buf->length)
        return;
    if (useUsbConsole) {
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
        if (usb_serial_jtag_is_connected()) {
            usb_serial_jtag_write_bytes((const char *)buf->data, buf->length, pdMS_TO_TICKS(20));
        } else {
            pxt::sendSerial((const char *)buf->data, buf->length);
        }
#else
        pxt::sendSerial((const char *)buf->data, buf->length);
#endif
    } else {
        uart_write_bytes(uartNum, (const char *)buf->data, buf->length);
    }
}

//%
SerialDevice internalCreateSerialDevice(DigitalInOutPin tx, DigitalInOutPin rx, int id) {
    int txPin = tx ? tx->getGpioNum() : -1;
    int rxPin = rx ? rx->getGpioNum() : -1;
    auto ser = new EspSerialDevice(id, txPin, rxPin);
    ser->next = serialDevices;
    serialDevices = ser;
    return ser;
}

} // namespace serial

namespace SerialDeviceMethods {
//%
void redirect(SerialDevice device, DigitalInOutPin tx, DigitalInOutPin rx, BaudRate rate) {
    device->redirect(tx, rx, rate);
}
} // namespace SerialDeviceMethods

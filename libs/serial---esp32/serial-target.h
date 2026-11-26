#pragma once
#include "pxt.h"
#include "driver/uart.h"

#define CODAL_SERIAL_EVT_DELIM_MATCH 1
#define CODAL_SERIAL_EVT_HEAD_MATCH 2
#define CODAL_SERIAL_EVT_RX_FULL 3
#define CODAL_SERIAL_EVT_DATA_RECEIVED 4

enum class SerialEvent;
enum class BaudRate;
enum class Delimiters;

namespace serial {

class EspSerialDevice {
    uint16_t id;
    uart_port_t uartNum;
    bool useUsbConsole;
    int baudRate;
    int delim;
    uint8_t *buffer;
    unsigned readp, writep, buffersz;
    SemaphoreHandle_t lock;

    void startRxTask();
    static void rxTaskFn(void *arg);
    void rxLoop();
    int bufferedSize();
    int readBuf(void *buf, int sz);

  public:
    EspSerialDevice *next;

    EspSerialDevice(uint16_t _id, int txPin = -1, int rxPin = -1);

    void setBaudRate(int rate);
    void setRxBufferSize(unsigned size);
    void setTxBufferSize(unsigned size) {}
    void redirect(DigitalInOutPin tx, DigitalInOutPin rx, BaudRate rate);

    int read();
    Buffer readBuffer();
    void writeBuffer(Buffer buffer);

    void onEvent(SerialEvent event, Action handler) { registerWithDal(id, (int)event, handler); }
    void onDelimiterReceived(Delimiters delimiter, Action handler) {
        registerWithDal(id, CODAL_SERIAL_EVT_DELIM_MATCH, handler);
        delim = (int)delimiter;
    }

    bool matchPins(DigitalInOutPin tx, DigitalInOutPin rx) {
        (void)tx;
        (void)rx;
        return !useUsbConsole;
    }
};

} // namespace serial

typedef serial::EspSerialDevice *SerialDevice;
static inline SerialDevice asSerialDevice(TValue v) { return (SerialDevice)v; }

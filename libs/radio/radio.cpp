#include "pxt.h"

// micro:bit dal
#if defined(MICROBIT_H) 

#define CODAL_RADIO MicroBitRadio
#define DEVICE_OK MICROBIT_OK
#define DEVICE_NOT_SUPPORTED MICROBIT_NOT_SUPPORTED
#define CODAL_EVENT MicroBitEvent
#define CODAL_RADIO_MICROBIT_DAL 1

// any other NRF52 board
#elif defined(NRF52_SERIES)

#include "NRF52Radio.h"
#define CODAL_RADIO codal::NRF52Radio
#define CODAL_EVENT codal::Event

// ESP32 / ESP32-C6 via ESP-NOW
#elif defined(PXT_ESP32)

#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "freertos/queue.h"
#include <stddef.h>
#include <string.h>

#define DEVICE_OK 0
#ifndef DEVICE_NOT_SUPPORTED
#define DEVICE_NOT_SUPPORTED -1002
#endif
#ifndef CREATE_ONLY
#define CREATE_ONLY 0
#endif

#endif

using namespace pxt;

#ifndef MICROBIT_RADIO_MAX_PACKET_SIZE
#define MICROBIT_RADIO_MAX_PACKET_SIZE          32
#endif

#ifndef DEVICE_RADIO_MAX_PACKET_SIZE
#define DEVICE_RADIO_MAX_PACKET_SIZE MICROBIT_RADIO_MAX_PACKET_SIZE
#endif

#ifndef MICROBIT_ID_RADIO
#define MICROBIT_ID_RADIO               29
#endif

#ifndef DEVICE_ID_RADIO
#define DEVICE_ID_RADIO MICROBIT_ID_RADIO
#endif

#ifndef MICROBIT_RADIO_EVT_DATAGRAM
#define MICROBIT_RADIO_EVT_DATAGRAM             1       // Event to signal that a new datagram has been received.
#endif

#ifndef DEVICE_RADIO_EVT_DATAGRAM
#define DEVICE_RADIO_EVT_DATAGRAM MICROBIT_RADIO_EVT_DATAGRAM
#endif

#if defined(PXT_ESP32)
namespace pxt {

struct EspRadioEvent {
    int src;
    int value;
    EspRadioEvent(int s, int v, int flags = 0) : src(s), value(v) { (void)flags; }
};

struct EspRadioPacket {
    uint8_t len;
    int rssi;
    uint8_t data[DEVICE_RADIO_MAX_PACKET_SIZE];

    int length() const { return len; }
    const uint8_t *getBytes() const { return data; }
    int getRSSI() const { return rssi; }
};

class EspNowRadio {
    static constexpr uint8_t BCAST_MAC[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static constexpr uint8_t MAGIC_PREFIX = 'P';
    static constexpr uint8_t MAGIC_DATAGRAM = 'R';
    static constexpr uint8_t MAGIC_EVENT = 'E';
    static constexpr int DEFAULT_RSSI_DBM = -73;
    static constexpr uint32_t RX_QUEUE_DEPTH = 16;

    struct __attribute__((packed)) DatagramFrame {
        uint8_t magic[2];
        uint8_t group;
        uint8_t len;
        uint8_t data[DEVICE_RADIO_MAX_PACKET_SIZE];
    };

    struct __attribute__((packed)) EventFrame {
        uint8_t magic[2];
        uint8_t group;
        uint8_t reserved;
        int32_t src;
        int32_t value;
    };

    static constexpr int DATAGRAM_HEADER_SIZE = (int)offsetof(DatagramFrame, data);

    static bool active;
    static uint8_t groupId;
    static QueueHandle_t rxQueue;

    static void onEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
        if (!active || !data || len < DATAGRAM_HEADER_SIZE || data[0] != MAGIC_PREFIX)
            return;

        if (data[1] == MAGIC_EVENT && len == (int)sizeof(EventFrame)) {
            const auto *frame = (const EventFrame *)data;
            if (frame->group == groupId)
                pxt::raiseEvent(frame->src, frame->value);
            return;
        }

        if (data[1] == MAGIC_DATAGRAM) {
            const auto *frame = (const DatagramFrame *)data;
            int payloadLen = frame->len;
            if (frame->group != groupId || !rxQueue)
                return;
            if (payloadLen <= 0 || payloadLen > DEVICE_RADIO_MAX_PACKET_SIZE ||
                payloadLen > len - DATAGRAM_HEADER_SIZE)
                return;

            EspRadioPacket pkt = {};
            pkt.len = (uint8_t)payloadLen;
            pkt.rssi = (info && info->rx_ctrl) ? info->rx_ctrl->rssi : DEFAULT_RSSI_DBM;
            memcpy(pkt.data, frame->data, payloadLen);
            if (xQueueSend(rxQueue, &pkt, 0) == pdPASS)
                pxt::raiseEvent(DEVICE_ID_RADIO, DEVICE_RADIO_EVT_DATAGRAM);
        }
    }

  public:
    struct DatagramSubsys {
        EspRadioPacket recv() {
            EspRadioPacket pkt = {};
            pkt.rssi = DEFAULT_RSSI_DBM;
            if (rxQueue)
                xQueueReceive(rxQueue, &pkt, 0);
            return pkt;
        }

        void send(const uint8_t *buf, int len) {
            if (!active || !buf || len <= 0 || len > DEVICE_RADIO_MAX_PACKET_SIZE)
                return;
            DatagramFrame frame;
            frame.magic[0] = MAGIC_PREFIX;
            frame.magic[1] = MAGIC_DATAGRAM;
            frame.group = groupId;
            frame.len = (uint8_t)len;
            memcpy(frame.data, buf, len);
            esp_now_send(BCAST_MAC, (const uint8_t *)&frame, DATAGRAM_HEADER_SIZE + len);
        }
    } datagram;

    struct EventSubsys {
        void eventReceived(EspRadioEvent ev) {
            if (!active)
                return;
            EventFrame frame = {{MAGIC_PREFIX, MAGIC_EVENT}, groupId, 0, ev.src, ev.value};
            esp_now_send(BCAST_MAC, (const uint8_t *)&frame, sizeof(frame));
        }
    } event;

    int enable() {
        if (!rxQueue)
            rxQueue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(EspRadioPacket));

        (void)esp_netif_init();
        esp_err_t evErr = esp_event_loop_create_default();
        if (evErr != ESP_OK && evErr != ESP_ERR_INVALID_STATE)
            return DEVICE_NOT_SUPPORTED;

        wifi_mode_t mode = WIFI_MODE_NULL;
        if (esp_wifi_get_mode(&mode) == ESP_ERR_WIFI_NOT_INIT) {
            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            if (esp_wifi_init(&cfg) != ESP_OK)
                return DEVICE_NOT_SUPPORTED;
            esp_wifi_set_storage(WIFI_STORAGE_RAM);
            esp_wifi_set_mode(WIFI_MODE_STA);
            if (esp_wifi_start() != ESP_OK)
                return DEVICE_NOT_SUPPORTED;
        } else if (mode == WIFI_MODE_NULL) {
            esp_wifi_set_mode(WIFI_MODE_STA);
            esp_wifi_start();
        }

        esp_err_t nowErr = esp_now_init();
        if (nowErr != ESP_OK && nowErr != ESP_ERR_ESPNOW_EXIST)
            return DEVICE_NOT_SUPPORTED;

        esp_now_register_recv_cb(onEspNowRecv);

        if (!esp_now_is_peer_exist(BCAST_MAC)) {
            esp_now_peer_info_t peer = {};
            memcpy(peer.peer_addr, BCAST_MAC, sizeof(BCAST_MAC));
            peer.channel = 0;
            peer.ifidx = WIFI_IF_STA;
            peer.encrypt = false;
            esp_now_add_peer(&peer);
        }

        active = true;
        return DEVICE_OK;
    }

    int disable() {
        active = false;
        return DEVICE_OK;
    }

    void setGroup(int id) {
        groupId = (uint8_t)(id & 0xff);
    }

    void setTransmitPower(int power) {
        if (power < 0) power = 0;
        if (power > 7) power = 7;
        // ESP-IDF TX power is in 0.25 dBm units: 0..7 maps to 8..78 (2.0 dBm .. 19.5 dBm)
        esp_wifi_set_max_tx_power((int8_t)(8 + power * 10));
    }

    void setFrequencyBand(int band) {
        if (band < 0) band = 0;
        if (band > 83) band = 83;
        // Map 2400..2483 MHz (1 MHz steps) to 2.4 GHz Wi-Fi channels 1..13 (5 MHz spacing)
        int channel = (band <= 12) ? 1 : ((band - 12) / 5 + 1);
        if (channel > 13) channel = 13;
        esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE);
    }
};

constexpr uint8_t EspNowRadio::BCAST_MAC[6];
bool EspNowRadio::active = false;
uint8_t EspNowRadio::groupId = 0;
QueueHandle_t EspNowRadio::rxQueue = NULL;

} // namespace pxt

#define CODAL_RADIO pxt::EspNowRadio
#define CODAL_EVENT pxt::EspRadioEvent
#define CODAL_RADIO_HAS_RSSI 1
#endif

//% color=#E3008C weight=96 icon="\uf012"
namespace radio {
    
#if CODAL_RADIO_MICROBIT_DAL
    CODAL_RADIO* getRadio() {
        return &uBit.radio;
    }
#elif defined(PXT_ESP32)
    CODAL_RADIO* getRadio() {
        static CODAL_RADIO instance;
        return &instance;
    }
#elif defined(CODAL_RADIO)
class RadioWrap {
    CODAL_RADIO radio;
    public:
        RadioWrap() 
            : radio()
        {}

    CODAL_RADIO* getRadio() {
        return &radio;
    }
};
SINGLETON(RadioWrap);
CODAL_RADIO* getRadio() {
    auto wrap = getRadioWrap();
    if (NULL != wrap)
        return wrap->getRadio();    
    return NULL;
}
#endif // #else

    bool radioEnabled = false;
    bool init = false;
    int radioEnable() {
#ifdef CODAL_RADIO
        auto radio = getRadio();
        if (NULL == radio) 
            return DEVICE_NOT_SUPPORTED;

        if (init && !radioEnabled) {
            //If radio was explicitly disabled from a call to off API
            //We don't want to enable it here. User needs to call on API first.
            return DEVICE_NOT_SUPPORTED;
        }

        int r = radio->enable();
        if (r != DEVICE_OK) {
            target_panic(43);
            return r;
        }
        if (!init) {
            getRadio()->setGroup(0); //Default group zero. This used to be pxt::programHash()
            getRadio()->setTransmitPower(6); // start with high power by default
            init = true;
        }
        radioEnabled = true;
        return r;
#else
        return DEVICE_NOT_SUPPORTED;
#endif
    }

    /**
    * Disables the radio for use as a multipoint sender/receiver.
    * Disabling radio will help conserve battery power when it is not in use.
    */
    //% help=radio/off
    void off() {
#ifdef CODAL_RADIO
        auto radio = getRadio();
        if (NULL == radio)
            return;

        int r = radio->disable();
        if (r != DEVICE_OK) {
            target_panic(43);
        } else {
            radioEnabled = false;
        }
#else
        return;
#endif
    }

    /**
    * Initialises the radio for use as a multipoint sender/receiver
    * Only useful when the radio.off() is used beforehand.
    */
    //% help=radio/on
    void on() {
#ifdef CODAL_RADIO
        auto radio = getRadio();
        if (NULL == radio)
            return;

        int r = radio->enable();
        if (r != DEVICE_OK) {
            target_panic(43);
        } else {
            radioEnabled = true;
        }
#else
        return;
#endif
    }

    /**
    * Sends an event over radio to neigboring devices
    */
    //% blockId=radioRaiseEvent block="radio raise event|from source %src=control_event_source_id|with value %value=control_event_value_id"
    //% src.label="source" value.label="value"
    //% blockExternalInputs=1
    //% advanced=true
    //% weight=1
    //% help=radio/raise-event
    void raiseEvent(int src, int value) {
#ifdef CODAL_RADIO        
        if (radioEnable() != DEVICE_OK) return;

        getRadio()->event.eventReceived(CODAL_EVENT(src, value, CREATE_ONLY));
#endif        
    }

    /**
     * Internal use only. Takes the next packet from the radio queue and returns its contents + RSSI in a Buffer.
     * @returns NULL if no packet available
     */
    //%
    Buffer readRawPacket() {
#ifdef CODAL_RADIO        
        if (radioEnable() != DEVICE_OK) return NULL;

        auto p = getRadio()->datagram.recv();
#if CODAL_RADIO_MICROBIT_DAL
        if (p == PacketBuffer::EmptyPacket)
            return NULL;
        int rssi = p.getRSSI();
        auto length = p.length();
        auto bytes = p.getBytes();
#elif CODAL_RADIO_HAS_RSSI
        auto length = p.length();
        if (length == 0)
            return NULL;
        int rssi = p.getRSSI();
        auto bytes = p.getBytes();
#else
        // TODO: RSSI support
        int rssi = -73;        
        auto length = p.length();
        auto bytes = p.getBytes();
        if (length == 0)
            return NULL;
#endif

        uint8_t buf[DEVICE_RADIO_MAX_PACKET_SIZE + sizeof(int)]; // packet length + rssi
        memset(buf, 0, sizeof(buf));
        memcpy(buf, bytes, length); // data
        memcpy(buf + DEVICE_RADIO_MAX_PACKET_SIZE, &rssi, sizeof(int)); // RSSi - assumes Int32LE layout
        return mkBuffer(buf, sizeof(buf));
#else
        return NULL;
#endif        
    }

    /**
     * Internal use only. Sends a raw packet through the radio (assumes RSSI appened to packet)
     */
    //% async
    void sendRawPacket(Buffer msg) {
#ifdef CODAL_RADIO        
        if (radioEnable() != DEVICE_OK || NULL == msg) return;

        // don't send RSSI data; and make sure no buffer underflow
        int len = msg->length - (int)sizeof(int);
        if (len > 0 && len <= DEVICE_RADIO_MAX_PACKET_SIZE)
            getRadio()->datagram.send(msg->data, len);
#endif            
    }

    /**
     * Used internally by the library.
     */
    //% help=radio/on-data-received
    //% weight=0
    //% blockId=radio_datagram_received_event block="radio on data received" blockGap=8
    //% deprecated=true blockHidden=1
    void onDataReceived(Action body) {
#ifdef CODAL_RADIO        
        if (radioEnable() != DEVICE_OK) return;

        registerWithDal(DEVICE_ID_RADIO, DEVICE_RADIO_EVT_DATAGRAM, body);
        getRadio()->datagram.recv(); // wake up read code
#endif       
    }

    /**
     * Sets the group id for radio communications. A micro:bit can only listen to one group ID at any time.
     * @param id the group id between ``0`` and ``255``, eg: 1
     */
    //% help=radio/set-group
    //% weight=100
    //% blockId=radio_set_group block="radio set group %ID"
    //% id.label="value"
    //% id.min=0 id.max=255
    //% group="Group"
    void setGroup(int id) {
#ifdef CODAL_RADIO        
        if (radioEnable() != DEVICE_OK) return;

        getRadio()->setGroup(id);
#endif       
    }

    /**
     * Change the output power level of the transmitter to the given value.
    * @param power a value in the range 0..7, where 0 is the lowest power and 7 is the highest. eg: 7
    */
    //% help=radio/set-transmit-power
    //% weight=9 blockGap=8
    //% blockId=radio_set_transmit_power block="radio set transmit power %power"
    //% power.label="value"
    //% power.min=0 power.max=7
    //% advanced=true
    void setTransmitPower(int power) {
#ifdef CODAL_RADIO        
        if (radioEnable() != DEVICE_OK) return;

        getRadio()->setTransmitPower(power);
#endif        
    }

    /**
    * Change the transmission and reception band of the radio to the given channel
    * @param band a frequency band in the range 0 - 83. Each step is 1MHz wide, based at 2400MHz.
    **/
    //% help=radio/set-frequency-band
    //% weight=8 blockGap=8
    //% blockId=radio_set_frequency_band block="radio set frequency band %band"
    //% band.label="value"
    //% band.min=0 band.max=83
    //% advanced=true
    void setFrequencyBand(int band) {
#ifdef CODAL_RADIO        
        if (radioEnable() != DEVICE_OK) return;
        getRadio()->setFrequencyBand(band);
#endif        
    }
}

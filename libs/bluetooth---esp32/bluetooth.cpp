#include "pxt.h"

#if CONFIG_BT_NIMBLE_ENABLED
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/queue.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>
#endif

namespace _ble {

static constexpr int BLE_EVENT_SOURCE_ID = 1235;
static constexpr int BLE_EVT_CONNECTED = 1;
static constexpr int BLE_EVT_DISCONNECTED = 2;
static constexpr int BLE_EVT_UART_RX = 3;
static constexpr int BLE_EVT_ADV_RECEIVED = 4;

static constexpr int MAX_UART_PACKET_LEN = 244;
static constexpr int MAX_ADV_DATA_LEN = 24;
static constexpr int MAX_RAW_ADV_LEN = 31;
static constexpr uint32_t RX_QUEUE_DEPTH = 16;

/** Get the event source ID used for BLE events. */
//%
int eventID() {
    return BLE_EVENT_SOURCE_ID;
}

#if CONFIG_BT_NIMBLE_ENABLED

struct UartRxItem {
    uint16_t len;
    uint8_t data[MAX_UART_PACKET_LEN];
};

struct AdvRxItem {
    int8_t rssi;
    uint8_t addr[6];
    uint8_t len;
    uint8_t data[MAX_RAW_ADV_LEN];
};

class EspBleController {
    bool initialized = false;
    bool synced = false;
    bool connected = false;
    bool uartAdvertising = false;
    bool beaconAdvertising = false;
    bool scanning = false;

    uint8_t ownAddrType = BLE_OWN_ADDR_PUBLIC;
    uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;
    uint16_t nusTxHandle = 0;
    uint16_t beaconIntervalUnits = 160; // 100ms in 0.625ms units
    uint8_t beaconPayload[MAX_ADV_DATA_LEN] = {};
    uint8_t beaconPayloadLen = 0;

    char deviceName[32] = "XIAO-C6";
    QueueHandle_t uartRxQueue = NULL;
    QueueHandle_t advRxQueue = NULL;

    // Nordic UART Service UUIDs (little-endian byte order for NimBLE):
    // Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
    // RX Char: 6E400002-B5A3-F393-E0A9-E50E24DCCA9E
    // TX Char: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E
    static const ble_uuid128_t NUS_SVC_UUID;
    static const ble_uuid128_t NUS_RX_UUID;
    static const ble_uuid128_t NUS_TX_UUID;

    static EspBleController &instance() {
        static EspBleController inst;
        return inst;
    }

    static int gattNusAccess(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
        (void)conn_handle;
        (void)attr_handle;
        (void)arg;
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && ctxt->om) {
            uint16_t omLen = OS_MBUF_PKTLEN(ctxt->om);
            if (omLen > 0) {
                UartRxItem item = {};
                uint16_t copyLen = omLen > MAX_UART_PACKET_LEN ? MAX_UART_PACKET_LEN : omLen;
                uint16_t outLen = 0;
                if (ble_hs_mbuf_to_flat(ctxt->om, item.data, copyLen, &outLen) == 0 && outLen > 0) {
                    item.len = outLen;
                    auto &self = instance();
                    if (self.uartRxQueue && xQueueSend(self.uartRxQueue, &item, 0) == pdPASS) {
                        pxt::raiseEvent(BLE_EVENT_SOURCE_ID, BLE_EVT_UART_RX);
                    }
                }
            }
        }
        return 0;
    }

    void startUartAdvertisingLocked() {
        if (!synced)
            return;
        ble_gap_adv_stop();

        struct ble_hs_adv_fields fields = {};
        fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
        fields.name = (uint8_t *)deviceName;
        fields.name_len = (uint8_t)strlen(deviceName);
        fields.name_is_complete = 1;
        fields.uuids128 = (ble_uuid128_t *)&NUS_SVC_UUID;
        fields.num_uuids128 = 1;
        fields.uuids128_is_complete = 1;

        if (ble_gap_adv_set_fields(&fields) != 0)
            return;

        struct ble_gap_adv_params advParams = {};
        advParams.conn_mode = BLE_GAP_CONN_MODE_UND;
        advParams.disc_mode = BLE_GAP_DISC_MODE_GEN;
        ble_gap_adv_start(ownAddrType, NULL, BLE_HS_FOREVER, &advParams, onGapEvent, NULL);
    }

    void startBeaconAdvertisingLocked() {
        if (!synced || beaconPayloadLen == 0)
            return;
        ble_gap_adv_stop();

        uint8_t raw[MAX_RAW_ADV_LEN] = {};
        uint8_t pos = 0;
        // Flags AD structure: len=2, type=0x01, flags=0x06
        raw[pos++] = 2;
        raw[pos++] = 0x01;
        raw[pos++] = 0x06;
        // Manufacturer Specific Data AD structure: type=0xFF, company=0xFFFF, payload...
        raw[pos++] = (uint8_t)(1 + 2 + beaconPayloadLen);
        raw[pos++] = 0xFF;
        raw[pos++] = 0xFF;
        raw[pos++] = 0xFF;
        memcpy(raw + pos, beaconPayload, beaconPayloadLen);
        pos += beaconPayloadLen;

        if (ble_gap_adv_set_data(raw, pos) != 0)
            return;

        struct ble_gap_adv_params advParams = {};
        advParams.conn_mode = BLE_GAP_CONN_MODE_NON;
        advParams.disc_mode = BLE_GAP_DISC_MODE_GEN;
        advParams.itvl_min = beaconIntervalUnits;
        advParams.itvl_max = beaconIntervalUnits;
        ble_gap_adv_start(ownAddrType, NULL, BLE_HS_FOREVER, &advParams, onGapEvent, NULL);
    }

    void startScanningLocked() {
        if (!synced)
            return;
        struct ble_gap_disc_params discParams = {};
        discParams.passive = 1;
        discParams.filter_duplicates = 0;
        ble_gap_disc(ownAddrType, BLE_HS_FOREVER, &discParams, onGapEvent, NULL);
    }

    static void extractAdvPayload(const uint8_t *adv, uint8_t advLen, AdvRxItem &out) {
        // Prefer Manufacturer Specific Data payload (skip 2-byte company ID) if present
        uint8_t i = 0;
        while (i + 1 < advLen) {
            uint8_t fieldLen = adv[i];
            if (fieldLen == 0 || i + 1 + fieldLen > advLen)
                break;
            uint8_t fieldType = adv[i + 1];
            if (fieldType == 0xFF && fieldLen >= 4) {
                uint8_t payloadLen = (uint8_t)(fieldLen - 3);
                if (payloadLen > MAX_RAW_ADV_LEN)
                    payloadLen = MAX_RAW_ADV_LEN;
                out.len = payloadLen;
                memcpy(out.data, &adv[i + 4], payloadLen);
                return;
            }
            i += (uint8_t)(1 + fieldLen);
        }
        uint8_t copyLen = advLen > MAX_RAW_ADV_LEN ? MAX_RAW_ADV_LEN : advLen;
        out.len = copyLen;
        if (copyLen > 0)
            memcpy(out.data, adv, copyLen);
    }

    static int onGapEvent(struct ble_gap_event *event, void *arg) {
        (void)arg;
        auto &self = instance();
        switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                self.connected = true;
                self.connHandle = event->connect.conn_handle;
                pxt::raiseEvent(BLE_EVENT_SOURCE_ID, BLE_EVT_CONNECTED);
            } else {
                self.connected = false;
                self.connHandle = BLE_HS_CONN_HANDLE_NONE;
                if (self.uartAdvertising)
                    self.startUartAdvertisingLocked();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            self.connected = false;
            self.connHandle = BLE_HS_CONN_HANDLE_NONE;
            pxt::raiseEvent(BLE_EVENT_SOURCE_ID, BLE_EVT_DISCONNECTED);
            if (self.uartAdvertising)
                self.startUartAdvertisingLocked();
            break;

        case BLE_GAP_EVENT_DISC: {
            if (!self.scanning || !self.advRxQueue)
                break;
            AdvRxItem item = {};
            item.rssi = event->disc.rssi;
            memcpy(item.addr, event->disc.addr.val, 6);
            extractAdvPayload(event->disc.data, event->disc.length_data, item);
            if (item.len > 0 && xQueueSend(self.advRxQueue, &item, 0) == pdPASS) {
                pxt::raiseEvent(BLE_EVENT_SOURCE_ID, BLE_EVT_ADV_RECEIVED);
            }
            break;
        }

        default:
            break;
        }
        return 0;
    }

    static void onSync() {
        auto &self = instance();
        ble_hs_id_infer_auto(0, &self.ownAddrType);
        self.synced = true;
        if (self.uartAdvertising)
            self.startUartAdvertisingLocked();
        else if (self.beaconAdvertising)
            self.startBeaconAdvertisingLocked();
        if (self.scanning)
            self.startScanningLocked();
    }

    static void bleHostTask(void *param) {
        (void)param;
        nimble_port_run();
        nimble_port_freertos_deinit();
    }

    void ensureInit() {
        if (initialized)
            return;

        if (!uartRxQueue)
            uartRxQueue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(UartRxItem));
        if (!advRxQueue)
            advRxQueue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(AdvRxItem));

        esp_err_t nvsErr = nvs_flash_init();
        if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES || nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            nvs_flash_init();
        }

        if (nimble_port_init() != ESP_OK)
            return;

        ble_svc_gap_init();
        ble_svc_gatt_init();
        ble_svc_gap_device_name_set(deviceName);

        static struct ble_gatt_chr_def nusChars[3] = {};
        nusChars[0].uuid = &NUS_RX_UUID.u;
        nusChars[0].access_cb = gattNusAccess;
        nusChars[0].flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP;

        nusChars[1].uuid = &NUS_TX_UUID.u;
        nusChars[1].access_cb = gattNusAccess;
        nusChars[1].val_handle = &nusTxHandle;
        nusChars[1].flags = BLE_GATT_CHR_F_NOTIFY;

        static struct ble_gatt_svc_def gattSvcs[2] = {};
        gattSvcs[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
        gattSvcs[0].uuid = &NUS_SVC_UUID.u;
        gattSvcs[0].characteristics = nusChars;

        ble_gatts_count_cfg(gattSvcs);
        ble_gatts_add_svcs(gattSvcs);

        ble_hs_cfg.sync_cb = onSync;
        nimble_port_freertos_init(bleHostTask);
        initialized = true;
    }

  public:
    static EspBleController &get() { return instance(); }

    void startUartService() {
        ensureInit();
        beaconAdvertising = false;
        uartAdvertising = true;
        startUartAdvertisingLocked();
    }

    int uartWriteBuffer(Buffer data) {
        if (!data || data->length == 0)
            return 0;
        if (!connected || connHandle == BLE_HS_CONN_HANDLE_NONE || nusTxHandle == 0)
            return -1;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(data->data, (uint16_t)data->length);
        if (!om)
            return -2;
        return ble_gatts_notify_custom(connHandle, nusTxHandle, om);
    }

    Buffer uartReadBuffer() {
        if (!uartRxQueue)
            return NULL;
        UartRxItem item = {};
        if (xQueueReceive(uartRxQueue, &item, 0) != pdPASS)
            return NULL;
        return mkBuffer(item.data, item.len);
    }

    bool isConnected() const { return connected; }

    void setDeviceName(String name) {
        if (!name)
            return;
        strlcpy(deviceName, name->getUTF8Data(), sizeof(deviceName));
        if (initialized)
            ble_svc_gap_device_name_set(deviceName);
    }

    void advertiseData(Buffer data, int intervalMs) {
        ensureInit();
        if (!data || data->length == 0)
            return;
        uint32_t len = data->length > MAX_ADV_DATA_LEN ? MAX_ADV_DATA_LEN : data->length;
        memcpy(beaconPayload, data->data, len);
        beaconPayloadLen = (uint8_t)len;
        if (intervalMs < 20)
            intervalMs = 20;
        if (intervalMs > 10000)
            intervalMs = 10000;
        beaconIntervalUnits = (uint16_t)((intervalMs * 1000) / 625);
        uartAdvertising = false;
        beaconAdvertising = true;
        startBeaconAdvertisingLocked();
    }

    void stopAdvertising() {
        uartAdvertising = false;
        beaconAdvertising = false;
        if (synced)
            ble_gap_adv_stop();
    }

    void startScanning() {
        ensureInit();
        scanning = true;
        startScanningLocked();
    }

    void stopScanning() {
        scanning = false;
        if (synced)
            ble_gap_disc_cancel();
    }

    Buffer readAdvertisement() {
        if (!advRxQueue)
            return NULL;
        AdvRxItem item = {};
        if (xQueueReceive(advRxQueue, &item, 0) != pdPASS)
            return NULL;
        // Layout: [rssi:i8 (1B), addr:u8[6] (6B), len:u8 (1B), data:u8[len]]
        uint8_t buf[8 + MAX_RAW_ADV_LEN];
        buf[0] = (uint8_t)item.rssi;
        memcpy(buf + 1, item.addr, 6);
        buf[7] = item.len;
        memcpy(buf + 8, item.data, item.len);
        return mkBuffer(buf, 8 + item.len);
    }
};

const ble_uuid128_t EspBleController::NUS_SVC_UUID = {
    {BLE_UUID_TYPE_128},
    {0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e}};
const ble_uuid128_t EspBleController::NUS_RX_UUID = {
    {BLE_UUID_TYPE_128},
    {0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e}};
const ble_uuid128_t EspBleController::NUS_TX_UUID = {
    {BLE_UUID_TYPE_128},
    {0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e}};

#endif // CONFIG_BT_NIMBLE_ENABLED

/** Initialize BLE stack and start Nordic UART Service advertising. */
//%
void startUartService() {
#if CONFIG_BT_NIMBLE_ENABLED
    EspBleController::get().startUartService();
#endif
}

/** Write a buffer over Nordic UART Service TX notifications. */
//%
int uartWriteBuffer(Buffer data) {
#if CONFIG_BT_NIMBLE_ENABLED
    return EspBleController::get().uartWriteBuffer(data);
#else
    (void)data;
    return -1;
#endif
}

/** Read the next received Nordic UART Service RX packet, or null if empty. */
//%
Buffer uartReadBuffer() {
#if CONFIG_BT_NIMBLE_ENABLED
    return EspBleController::get().uartReadBuffer();
#else
    return NULL;
#endif
}

/** Check whether a BLE central is currently connected. */
//%
bool isConnected() {
#if CONFIG_BT_NIMBLE_ENABLED
    return EspBleController::get().isConnected();
#else
    return false;
#endif
}

/** Set the BLE GAP device name. */
//%
void setDeviceName(String name) {
#if CONFIG_BT_NIMBLE_ENABLED
    EspBleController::get().setDeviceName(name);
#else
    (void)name;
#endif
}

/** Broadcast non-connectable BLE manufacturer data (up to 24 bytes). */
//%
void advertiseData(Buffer data, int intervalMs) {
#if CONFIG_BT_NIMBLE_ENABLED
    EspBleController::get().advertiseData(data, intervalMs);
#else
    (void)data;
    (void)intervalMs;
#endif
}

/** Stop BLE advertising. */
//%
void stopAdvertising() {
#if CONFIG_BT_NIMBLE_ENABLED
    EspBleController::get().stopAdvertising();
#endif
}

/** Start scanning for BLE advertisements. */
//%
void startScanning() {
#if CONFIG_BT_NIMBLE_ENABLED
    EspBleController::get().startScanning();
#endif
}

/** Stop scanning for BLE advertisements. */
//%
void stopScanning() {
#if CONFIG_BT_NIMBLE_ENABLED
    EspBleController::get().stopScanning();
#endif
}

/** Read the next queued BLE advertisement packet: [rssi:i8, addr:u8[6], len:u8, data:u8[len]]. */
//%
Buffer readAdvertisement() {
#if CONFIG_BT_NIMBLE_ENABLED
    return EspBleController::get().readAdvertisement();
#else
    return NULL;
#endif
}

} // namespace _ble

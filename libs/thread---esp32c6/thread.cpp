#include "pxt.h"

#if CONFIG_OPENTHREAD_ENABLED
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_vfs_eventfd.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "openthread/dataset.h"
#include "openthread/instance.h"
#include "openthread/ip6.h"
#include "openthread/thread.h"
#include "openthread/udp.h"
#include <string.h>
#endif

extern "C" int pxt_802154_claim(int mode);

namespace _thread {

static constexpr int THREAD_EVENT_SOURCE_ID = 1236;
static constexpr int THREAD_EVT_PACKET = 1;
static constexpr int THREAD_EVT_ROLE_CHANGED = 2;

static constexpr uint16_t THREAD_INTERCOMM_PORT = 12350;
static constexpr int MAX_THREAD_PACKET_LEN = 240;
static constexpr uint32_t RX_QUEUE_DEPTH = 16;

/** Get event source ID for Thread events. */
//%
int eventID() {
    return THREAD_EVENT_SOURCE_ID;
}

#if CONFIG_OPENTHREAD_ENABLED

struct ThreadRxItem {
    uint16_t len;
    uint8_t data[MAX_THREAD_PACKET_LEN];
};

class EspThreadController {
    bool initialized = false;
    bool started = false;
    otUdpSocket udpSocket = {};
    QueueHandle_t rxQueue = NULL;
    esp_netif_t *otNetif = NULL;

    static EspThreadController &instance() {
        static EspThreadController inst;
        return inst;
    }

    static void onUdpReceive(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo) {
        (void)aContext;
        (void)aMessageInfo;
        auto &self = instance();
        if (!self.rxQueue || !aMessage)
            return;

        uint16_t len = otMessageGetLength(aMessage) - otMessageGetOffset(aMessage);
        if (len == 0 || len > MAX_THREAD_PACKET_LEN)
            return;

        ThreadRxItem item = {};
        item.len = len;
        otMessageRead(aMessage, otMessageGetOffset(aMessage), item.data, len);
        if (xQueueSend(self.rxQueue, &item, 0) == pdPASS) {
            pxt::raiseEvent(THREAD_EVENT_SOURCE_ID, THREAD_EVT_PACKET);
        }
    }

    static void onStateChanged(otChangedFlags flags, void *ctx) {
        (void)ctx;
        if (flags & OT_CHANGED_THREAD_ROLE) {
            pxt::raiseEvent(THREAD_EVENT_SOURCE_ID, THREAD_EVT_ROLE_CHANGED);
        }
    }

    static void otMainTask(void *arg) {
        (void)arg;
        esp_openthread_launch_mainloop();
        esp_openthread_netif_glue_deinit();
        vTaskDelete(NULL);
    }

    bool ensureInit() {
        if (initialized)
            return true;
        if (pxt_802154_claim(1) != 0)
            return false;

        if (!rxQueue)
            rxQueue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(ThreadRxItem));

        esp_err_t nvsErr = nvs_flash_init();
        if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES || nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            nvs_flash_init();
        }

        (void)esp_netif_init();
        esp_err_t evErr = esp_event_loop_create_default();
        if (evErr != ESP_OK && evErr != ESP_ERR_INVALID_STATE)
            return false;

        esp_vfs_eventfd_config_t eventfdCfg = {};
        eventfdCfg.max_fds = 3;
        esp_err_t fdErr = esp_vfs_eventfd_register(&eventfdCfg);
        if (fdErr != ESP_OK && fdErr != ESP_ERR_INVALID_STATE)
            return false;

        esp_openthread_platform_config_t cfg = {};
        cfg.radio_config.radio_mode = RADIO_MODE_NATIVE;
        cfg.host_config.host_connection_mode = HOST_CONNECTION_MODE_NONE;
        cfg.port_config.storage_partition_name = "nvs";
        cfg.port_config.netif_queue_size = 10;
        cfg.port_config.task_queue_size = 10;

        if (esp_openthread_init(&cfg) != ESP_OK)
            return false;

        esp_netif_config_t netifCfg = ESP_NETIF_DEFAULT_OPENTHREAD();
        otNetif = esp_netif_new(&netifCfg);
        if (otNetif) {
            esp_netif_attach(otNetif, esp_openthread_netif_glue_init(&cfg));
        }

        otInstance *ot = esp_openthread_get_instance();
        if (!ot)
            return false;

        otSetStateChangedCallback(ot, onStateChanged, NULL);

        otSockAddr bindAddr = {};
        bindAddr.mPort = THREAD_INTERCOMM_PORT;
        otUdpOpen(ot, &udpSocket, onUdpReceive, NULL);
        otUdpBind(ot, &udpSocket, &bindAddr, OT_NETIF_THREAD_HOST);

        otIp6Address mcastAddr = {};
        otIp6AddressFromString("ff03::1", &mcastAddr);
        otIp6SubscribeMulticastAddress(ot, &mcastAddr);

        xTaskCreate(otMainTask, "ot_main", 6144, NULL, 5, NULL);
        initialized = true;
        return true;
    }

    static void deriveNetworkKey(const char *keyStr, uint8_t out[OT_NETWORK_KEY_SIZE]) {
        memset(out, 0, OT_NETWORK_KEY_SIZE);
        if (!keyStr || !keyStr[0])
            keyStr = "MakeCodeThread";
        size_t len = strlen(keyStr);
        for (size_t i = 0; i < OT_NETWORK_KEY_SIZE; ++i) {
            out[i] = (uint8_t)(keyStr[i % len] ^ (uint8_t)(i * 0x1d + 0x5a));
        }
    }

    static int hexVal(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

  public:
    static EspThreadController &get() { return instance(); }

    int joinGroup(int channel, int panId, String networkKey) {
        if (!ensureInit())
            return -1;

        if (channel < 11) channel = 11;
        if (channel > 26) channel = 26;

        otOperationalDataset dataset = {};
        dataset.mChannel = (uint16_t)channel;
        dataset.mComponents.mIsChannelPresent = true;
        dataset.mPanId = (otPanId)(panId & 0xffff);
        dataset.mComponents.mIsPanIdPresent = true;
        deriveNetworkKey(networkKey ? networkKey->getUTF8Data() : NULL, dataset.mNetworkKey.m8);
        dataset.mComponents.mIsNetworkKeyPresent = true;
        snprintf(dataset.mNetworkName.m8, sizeof(dataset.mNetworkName.m8), "MakeCode-%04X",
                 (unsigned)dataset.mPanId);
        dataset.mComponents.mIsNetworkNamePresent = true;

        esp_openthread_lock_acquire(portMAX_DELAY);
        otInstance *ot = esp_openthread_get_instance();
        otDatasetSetActive(ot, &dataset);
        otIp6SetEnabled(ot, true);
        otThreadSetEnabled(ot, true);
        esp_openthread_lock_release();
        started = true;
        return 0;
    }

    int joinDatasetHex(String datasetHex) {
        if (!ensureInit() || !datasetHex)
            return -1;
        const char *hex = datasetHex->getUTF8Data();
        size_t slen = strlen(hex);
        if (slen == 0 || (slen & 1) || slen / 2 > OT_OPERATIONAL_DATASET_MAX_LENGTH)
            return -2;

        otOperationalDatasetTlvs tlvs = {};
        tlvs.mLength = (uint8_t)(slen / 2);
        for (size_t i = 0; i < tlvs.mLength; ++i) {
            int hi = hexVal(hex[i * 2]);
            int lo = hexVal(hex[i * 2 + 1]);
            if (hi < 0 || lo < 0)
                return -2;
            tlvs.mTlvs[i] = (uint8_t)((hi << 4) | lo);
        }

        esp_openthread_lock_acquire(portMAX_DELAY);
        otInstance *ot = esp_openthread_get_instance();
        otDatasetSetActiveTlvs(ot, &tlvs);
        otIp6SetEnabled(ot, true);
        otThreadSetEnabled(ot, true);
        esp_openthread_lock_release();
        started = true;
        return 0;
    }

    int sendPacket(Buffer data) {
        if (!started || !data || data->length == 0 || data->length > MAX_THREAD_PACKET_LEN)
            return -1;

        esp_openthread_lock_acquire(portMAX_DELAY);
        otInstance *ot = esp_openthread_get_instance();
        otMessage *msg = otUdpNewMessage(ot, NULL);
        int res = -2;
        if (msg) {
            if (otMessageAppend(msg, data->data, (uint16_t)data->length) == OT_ERROR_NONE) {
                otMessageInfo info = {};
                otIp6AddressFromString("ff03::1", &info.mPeerAddr);
                info.mPeerPort = THREAD_INTERCOMM_PORT;
                if (otUdpSend(ot, &udpSocket, msg, &info) == OT_ERROR_NONE) {
                    res = (int)data->length;
                    msg = NULL;
                }
            }
            if (msg)
                otMessageFree(msg);
        }
        esp_openthread_lock_release();
        return res;
    }

    Buffer readPacket() {
        if (!rxQueue)
            return NULL;
        ThreadRxItem item = {};
        if (xQueueReceive(rxQueue, &item, 0) != pdPASS)
            return NULL;
        return mkBuffer(item.data, item.len);
    }

    int role() {
        if (!initialized)
            return 0;
        esp_openthread_lock_acquire(portMAX_DELAY);
        otDeviceRole r = otThreadGetDeviceRole(esp_openthread_get_instance());
        esp_openthread_lock_release();
        return (int)r;
    }
};

#endif // CONFIG_OPENTHREAD_ENABLED

/** Join or form a Thread mesh group on the given 802.15.4 channel (11..26) and PAN ID. */
//%
int joinGroup(int channel, int panId, String networkKey) {
#if CONFIG_OPENTHREAD_ENABLED
    return EspThreadController::get().joinGroup(channel, panId, networkKey);
#else
    (void)channel;
    (void)panId;
    (void)networkKey;
    return -1;
#endif
}

/** Join an existing Thread network using an Active Operational Dataset TLV hex string. */
//%
int joinDatasetHex(String datasetHex) {
#if CONFIG_OPENTHREAD_ENABLED
    return EspThreadController::get().joinDatasetHex(datasetHex);
#else
    (void)datasetHex;
    return -1;
#endif
}

/** Send a raw Thread mesh intercomm packet over IPv6 multicast (ff03::1). */
//%
int sendPacket(Buffer data) {
#if CONFIG_OPENTHREAD_ENABLED
    return EspThreadController::get().sendPacket(data);
#else
    (void)data;
    return -1;
#endif
}

/** Read the next queued Thread mesh intercomm packet, or null if empty. */
//%
Buffer readPacket() {
#if CONFIG_OPENTHREAD_ENABLED
    return EspThreadController::get().readPacket();
#else
    return NULL;
#endif
}

/** Get current OpenThread device role (0=Disabled..4=Leader). */
//%
int role() {
#if CONFIG_OPENTHREAD_ENABLED
    return EspThreadController::get().role();
#else
    return 0;
#endif
}

} // namespace _thread

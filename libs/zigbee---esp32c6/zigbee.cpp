#include "pxt.h"

#if CONFIG_ZB_ENABLED
#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include <string.h>
#endif

extern "C" int pxt_802154_claim(int mode);

namespace _zigbee {

static constexpr int ZIGBEE_EVENT_SOURCE_ID = 1237;
static constexpr int ZIGBEE_EVT_JOINED = 1;
static constexpr int ZIGBEE_EVT_PACKET = 2;
static constexpr int ZIGBEE_EVT_SWITCH = 3;

static constexpr uint8_t ZIGBEE_EP = 1;
static constexpr uint16_t CUSTOM_CLUSTER_ID = 0xFC00;
static constexpr uint8_t CUSTOM_CMD_ID = 0x00;
static constexpr int MAX_ZB_PACKET_LEN = 200;
static constexpr uint32_t RX_QUEUE_DEPTH = 16;

/** Get event source ID for Zigbee events. */
//%
int eventID() {
    return ZIGBEE_EVENT_SOURCE_ID;
}

#if CONFIG_ZB_ENABLED

struct ZbRxItem {
    uint16_t len;
    uint8_t data[MAX_ZB_PACKET_LEN];
};

class EspZigbeeController {
    bool initialized = false;
    bool joined = false;
    bool switchState = false;
    int roleMode = 0; // 0=Coordinator, 1=Router, 2=EndDevice
    uint32_t channelMask = 0;
    QueueHandle_t rxQueue = NULL;

    static EspZigbeeController &instance() {
        static EspZigbeeController inst;
        return inst;
    }

    static void bdbStartTopLevelCommissioningCb(uint8_t modeMask) {
        esp_zb_bdb_start_top_level_commissioning(modeMask);
    }

    static esp_err_t onZbAction(esp_zb_core_action_callback_id_t callbackId, const void *message) {
        auto &self = instance();
        if (callbackId == ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID && message) {
            const auto *msg = (const esp_zb_zcl_set_attr_value_message_t *)message;
            if (msg->info.status == ESP_ZB_ZCL_STATUS_SUCCESS &&
                msg->info.dst_endpoint == ZIGBEE_EP &&
                msg->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF &&
                msg->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID &&
                msg->attribute.data.value) {
                self.switchState = (*(const uint8_t *)msg->attribute.data.value) != 0;
                pxt::raiseEvent(ZIGBEE_EVENT_SOURCE_ID, ZIGBEE_EVT_SWITCH);
            }
        } else if (callbackId == ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID && message) {
            const auto *msg = (const esp_zb_zcl_custom_cluster_command_message_t *)message;
            if (msg->info.dst_endpoint == ZIGBEE_EP &&
                msg->info.cluster == CUSTOM_CLUSTER_ID &&
                msg->data.value && msg->data.size > 0 && self.rxQueue) {
                const uint8_t *raw = (const uint8_t *)msg->data.value;
                uint16_t len = msg->data.size;
                // ZCL OCTET_STRING may prefix 1-byte length when sent over APS; strip if matching
                if (len > 1 && raw[0] == (uint8_t)(len - 1)) {
                    raw++;
                    len--;
                }
                if (len > MAX_ZB_PACKET_LEN)
                    len = MAX_ZB_PACKET_LEN;
                ZbRxItem item = {};
                item.len = len;
                memcpy(item.data, raw, len);
                if (xQueueSend(self.rxQueue, &item, 0) == pdPASS) {
                    pxt::raiseEvent(ZIGBEE_EVENT_SOURCE_ID, ZIGBEE_EVT_PACKET);
                }
            }
        }
        return ESP_OK;
    }

    static void zigbeeTask(void *pvParameters) {
        (void)pvParameters;
        auto &self = instance();

        esp_zb_cfg_t zbCfg = {};
        zbCfg.install_code_policy = false;
        if (self.roleMode == 0) {
            zbCfg.esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR;
            zbCfg.nwk_cfg.zczr_cfg.max_children = 10;
        } else if (self.roleMode == 1) {
            zbCfg.esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER;
            zbCfg.nwk_cfg.zczr_cfg.max_children = 10;
        } else {
            zbCfg.esp_zb_role = ESP_ZB_DEVICE_TYPE_ED;
            zbCfg.nwk_cfg.zed_cfg.ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN;
            zbCfg.nwk_cfg.zed_cfg.keep_alive = 3000;
        }
        esp_zb_init(&zbCfg);

        esp_zb_ep_list_t *epList = esp_zb_ep_list_create();
        esp_zb_cluster_list_t *clusterList = esp_zb_zcl_cluster_list_create();

        esp_zb_basic_cluster_cfg_t basicCfg = {};
        basicCfg.zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE;
        basicCfg.power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE;
        esp_zb_cluster_list_add_basic_cluster(
            clusterList, esp_zb_basic_cluster_create(&basicCfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        esp_zb_identify_cluster_cfg_t idCfg = {};
        esp_zb_cluster_list_add_identify_cluster(
            clusterList, esp_zb_identify_cluster_create(&idCfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        esp_zb_on_off_cluster_cfg_t onOffCfg = {};
        esp_zb_cluster_list_add_on_off_cluster(
            clusterList, esp_zb_on_off_cluster_create(&onOffCfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        esp_zb_temperature_meas_cluster_cfg_t tempCfg = {};
        tempCfg.measured_value = 2500;
        tempCfg.min_value = -4000;
        tempCfg.max_value = 8500;
        esp_zb_cluster_list_add_temperature_meas_cluster(
            clusterList, esp_zb_temperature_meas_cluster_create(&tempCfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        esp_zb_attribute_list_t *customSrvAttrs = esp_zb_zcl_attr_list_create(CUSTOM_CLUSTER_ID);
        esp_zb_cluster_list_add_custom_cluster(clusterList, customSrvAttrs, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        esp_zb_attribute_list_t *customCliAttrs = esp_zb_zcl_attr_list_create(CUSTOM_CLUSTER_ID);
        esp_zb_cluster_list_add_custom_cluster(clusterList, customCliAttrs, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);

        esp_zb_endpoint_config_t epCfg = {};
        epCfg.endpoint = ZIGBEE_EP;
        epCfg.app_profile_id = ESP_ZB_AF_HA_PROFILE_ID;
        epCfg.app_device_id = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID;
        epCfg.app_device_version = 0;
        esp_zb_ep_list_add_ep(epList, clusterList, epCfg);

        esp_zb_device_register(epList);
        esp_zb_core_action_handler_register(onZbAction);
        esp_zb_set_primary_network_channel_set(self.channelMask);

        esp_zb_start(false);
        esp_zb_stack_main_loop();
    }

  public:
    static EspZigbeeController &get() { return instance(); }

    void handleSignal(esp_zb_app_signal_t *signalStruct) {
        uint32_t *pSg = signalStruct->p_app_signal;
        esp_err_t errStatus = signalStruct->esp_err_status;
        esp_zb_app_signal_type_t sigType = (esp_zb_app_signal_type_t)(*pSg);

        switch (sigType) {
        case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
            break;
        case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
            if (errStatus == ESP_OK) {
                uint8_t mode = (roleMode == 0) ? ESP_ZB_BDB_MODE_NETWORK_FORMATION
                                               : ESP_ZB_BDB_MODE_NETWORK_STEERING;
                esp_zb_bdb_start_top_level_commissioning(mode);
            }
            break;
        case ESP_ZB_BDB_SIGNAL_FORMATION:
            if (errStatus == ESP_OK) {
                joined = true;
                esp_zb_bdb_open_network(180);
                pxt::raiseEvent(ZIGBEE_EVENT_SOURCE_ID, ZIGBEE_EVT_JOINED);
            } else {
                esp_zb_scheduler_alarm(
                    bdbStartTopLevelCommissioningCb, ESP_ZB_BDB_MODE_NETWORK_FORMATION, 1000);
            }
            break;
        case ESP_ZB_BDB_SIGNAL_STEERING:
            if (errStatus == ESP_OK) {
                joined = true;
                pxt::raiseEvent(ZIGBEE_EVENT_SOURCE_ID, ZIGBEE_EVT_JOINED);
            } else {
                esp_zb_scheduler_alarm(
                    bdbStartTopLevelCommissioningCb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
            }
            break;
        default:
            break;
        }
    }

    int startNetwork(int role, int channel) {
        if (initialized)
            return 0;
        if (pxt_802154_claim(2) != 0)
            return -1;

        if (!rxQueue)
            rxQueue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(ZbRxItem));

        esp_err_t nvsErr = nvs_flash_init();
        if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES || nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            nvs_flash_init();
        }

        if (channel < 11) channel = 11;
        if (channel > 26) channel = 26;
        roleMode = (role >= 0 && role <= 2) ? role : 0;
        channelMask = (1UL << channel);

        esp_zb_platform_config_t platCfg = {};
        platCfg.radio_config.radio_mode = ZB_RADIO_MODE_NATIVE;
        platCfg.host_config.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE;
        if (esp_zb_platform_config(&platCfg) != ESP_OK)
            return -1;

        xTaskCreate(zigbeeTask, "zb_main", 6144, NULL, 5, NULL);
        initialized = true;
        return 0;
    }

    int sendPacket(Buffer data) {
        if (!initialized || !data || data->length == 0 || data->length > MAX_ZB_PACKET_LEN)
            return -1;

        uint8_t payload[1 + MAX_ZB_PACKET_LEN];
        payload[0] = (uint8_t)data->length;
        memcpy(payload + 1, data->data, data->length);

        esp_zb_zcl_custom_cluster_cmd_t cmdReq = {};
        cmdReq.zcl_basic_cmd.dst_addr_u.addr_short = 0xFFFF;
        cmdReq.zcl_basic_cmd.dst_endpoint = ZIGBEE_EP;
        cmdReq.zcl_basic_cmd.src_endpoint = ZIGBEE_EP;
        cmdReq.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
        cmdReq.profile_id = ESP_ZB_AF_HA_PROFILE_ID;
        cmdReq.cluster_id = CUSTOM_CLUSTER_ID;
        cmdReq.custom_cmd_id = CUSTOM_CMD_ID;
        cmdReq.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV;
        cmdReq.data.type = ESP_ZB_ZCL_ATTR_TYPE_OCTET_STRING;
        cmdReq.data.size = (uint16_t)(1 + data->length);
        cmdReq.data.value = payload;

        esp_zb_lock_acquire(portMAX_DELAY);
        esp_zb_zcl_custom_cluster_cmd_req(&cmdReq);
        esp_zb_lock_release();
        return (int)data->length;
    }

    Buffer readPacket() {
        if (!rxQueue)
            return NULL;
        ZbRxItem item = {};
        if (xQueueReceive(rxQueue, &item, 0) != pdPASS)
            return NULL;
        return mkBuffer(item.data, item.len);
    }

    void setSwitch(bool on) {
        switchState = on;
        if (!initialized)
            return;
        uint8_t val = on ? 1 : 0;
        esp_zb_lock_acquire(portMAX_DELAY);
        esp_zb_zcl_set_attribute_val(
            ZIGBEE_EP, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
            ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &val, false);
        esp_zb_lock_release();
    }

    bool getSwitch() const { return switchState; }

    void reportTemperature(int centiCelsius) {
        if (!initialized)
            return;
        int16_t measured = (int16_t)centiCelsius;
        esp_zb_lock_acquire(portMAX_DELAY);
        esp_zb_zcl_set_attribute_val(
            ZIGBEE_EP, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
            ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &measured, false);
        esp_zb_lock_release();
    }

    bool isJoined() const { return joined; }
};

extern "C" void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct) {
    EspZigbeeController::get().handleSignal(signal_struct);
}

#endif // CONFIG_ZB_ENABLED

/** Start Zigbee 3.0 stack with role (0=Coordinator, 1=Router, 2=EndDevice) on channel (11..26). */
//%
int startNetwork(int role, int channel) {
#if CONFIG_ZB_ENABLED
    return EspZigbeeController::get().startNetwork(role, channel);
#else
    (void)role;
    (void)channel;
    return -1;
#endif
}

/** Send a Zigbee intercomm packet over the custom MakeCode cluster (0xFC00). */
//%
int sendPacket(Buffer data) {
#if CONFIG_ZB_ENABLED
    return EspZigbeeController::get().sendPacket(data);
#else
    (void)data;
    return -1;
#endif
}

/** Read the next queued Zigbee intercomm packet, or null if empty. */
//%
Buffer readPacket() {
#if CONFIG_ZB_ENABLED
    return EspZigbeeController::get().readPacket();
#else
    return NULL;
#endif
}

/** Set the local ZCL On/Off cluster attribute and report to bound peers. */
//%
void setSwitch(bool on) {
#if CONFIG_ZB_ENABLED
    EspZigbeeController::get().setSwitch(on);
#else
    (void)on;
#endif
}

/** Get the current ZCL On/Off switch state. */
//%
bool getSwitch() {
#if CONFIG_ZB_ENABLED
    return EspZigbeeController::get().getSwitch();
#else
    return false;
#endif
}

/** Report temperature (in 0.01 degrees Celsius) on the ZCL Temperature Measurement cluster (0x0402). */
//%
void reportTemperature(int centiCelsius) {
#if CONFIG_ZB_ENABLED
    EspZigbeeController::get().reportTemperature(centiCelsius);
#else
    (void)centiCelsius;
#endif
}

/** Check whether the device has joined/formed a Zigbee network. */
//%
bool isJoined() {
#if CONFIG_ZB_ENABLED
    return EspZigbeeController::get().isJoined();
#else
    return false;
#endif
}

} // namespace _zigbee

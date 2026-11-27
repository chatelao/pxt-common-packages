#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_WIFI_NOT_INIT 0x3001
#define ESP_ERR_ESPNOW_EXIST 0x306a

#define ESP_ERROR_CHECK(x) do { esp_err_t __e = (x); if (__e != ESP_OK) abort(); } while (0)

typedef enum {
    WIFI_MODE_NULL = 0,
    WIFI_MODE_STA,
    WIFI_MODE_AP,
    WIFI_MODE_APSTA
} wifi_mode_t;

typedef enum {
    WIFI_IF_STA = 0,
    WIFI_IF_AP = 1
} wifi_interface_t;

typedef enum {
    WIFI_STORAGE_FLASH = 0,
    WIFI_STORAGE_RAM = 1
} wifi_storage_t;

typedef enum {
    WIFI_SECOND_CHAN_NONE = 0,
    WIFI_SECOND_CHAN_ABOVE = 1,
    WIFI_SECOND_CHAN_BELOW = 2
} wifi_second_chan_t;

typedef enum {
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WPA2_PSK = 3,
    WIFI_AUTH_WPA2_ENTERPRISE = 5
} wifi_auth_mode_t;

typedef struct {
    int dummy;
} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() { 0 }

typedef union {
    struct {
        uint8_t ssid[32];
        uint8_t password[64];
    } sta;
    struct {
        uint8_t ssid[32];
        uint8_t password[64];
        uint8_t ssid_len;
        uint8_t channel;
        wifi_auth_mode_t authmode;
        uint8_t max_connection;
    } ap;
} wifi_config_t;

typedef struct {
    int dummy;
} wifi_scan_config_t;

typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[33];
    uint8_t primary;
    wifi_second_chan_t second;
    int8_t rssi;
    wifi_auth_mode_t authmode;
    uint32_t phy_11b : 1;
    uint32_t phy_11g : 1;
    uint32_t phy_11n : 1;
    uint32_t phy_lr : 1;
    uint32_t phy_11ax : 1;
    uint32_t wps : 1;
} wifi_ap_record_t;

typedef struct {
    uint32_t addr;
} esp_ip4_addr_t;

typedef struct {
    esp_ip4_addr_t ip;
    esp_ip4_addr_t netmask;
    esp_ip4_addr_t gw;
} esp_netif_ip_info_t;

typedef struct {
    esp_netif_ip_info_t ip_info;
} ip_event_got_ip_t;

typedef struct esp_netif_obj esp_netif_t;
typedef struct { int dummy; } esp_netif_config_t;
#define ESP_NETIF_DEFAULT_WIFI_STA() { 0 }

typedef const char *esp_event_base_t;
typedef void (*esp_event_handler_t)(void *event_handler_arg, esp_event_base_t event_base,
                                    int32_t event_id, void *event_data);

extern esp_event_base_t WIFI_EVENT;
extern esp_event_base_t IP_EVENT;
#define WIFI_EVENT_SCAN_DONE 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define WIFI_EVENT_AP_START 3
#define WIFI_EVENT_AP_STOP 4
#define WIFI_EVENT_AP_STACONNECTED 5
#define WIFI_EVENT_AP_STADISCONNECTED 6
#define IP_EVENT_STA_GOT_IP 1

typedef struct {
    int rssi;
} wifi_pkt_rx_ctrl_t;

typedef struct {
    uint8_t *src_addr;
    uint8_t *des_addr;
    wifi_pkt_rx_ctrl_t *rx_ctrl;
} esp_now_recv_info_t;

typedef void (*esp_now_recv_cb_t)(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len);

typedef struct {
    uint8_t peer_addr[6];
    uint8_t channel;
    wifi_interface_t ifidx;
    bool encrypt;
} esp_now_peer_info_t;

// FreeRTOS stubs
typedef void *QueueHandle_t;
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define pdPASS 1
#define pdFAIL 0
#define pdMS_TO_TICKS(ms) (ms)

QueueHandle_t xQueueCreate(uint32_t uxQueueLength, uint32_t uxItemSize);
BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait);
BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait);
void vTaskDelay(TickType_t xTicksToDelay);

// ESP-IDF Wi-Fi / ESP-NOW / Netif / Event stubs
esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode);
esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_set_storage(wifi_storage_t storage);
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *ap_records);
esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info);
esp_err_t esp_wifi_set_max_tx_power(int8_t power);
esp_err_t esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t second);

esp_netif_t *esp_netif_new(const esp_netif_config_t *esp_netif_config);
esp_netif_t *esp_netif_create_default_wifi_ap(void);
esp_err_t esp_netif_get_ip_info(esp_netif_t *esp_netif, esp_netif_ip_info_t *ip_info);
esp_err_t esp_netif_attach_wifi_station(esp_netif_t *esp_netif);
esp_err_t esp_wifi_set_default_wifi_sta_handlers(void);
esp_err_t esp_event_handler_register(esp_event_base_t event_base, int32_t event_id,
                                     esp_event_handler_t event_handler, void *event_handler_arg);

esp_err_t esp_now_init(void);
esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb);
bool esp_now_is_peer_exist(const uint8_t *peer_addr);
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer);
esp_err_t esp_now_send(const uint8_t *peer_addr, const uint8_t *data, size_t len);

// GPIO stubs
typedef int gpio_num_t;
typedef enum { GPIO_MODE_OUTPUT = 2 } gpio_mode_t;
esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode);
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);

// Logging stubs
typedef enum { ESP_LOG_INFO = 3 } esp_log_level_t;
void esp_log_level_set(const char *tag, esp_log_level_t level);
#define ESP_LOGI(tag, fmt, ...) ((void)0)

// TLS stubs
typedef struct esp_tls_error_handle *esp_tls_error_handle_t;
typedef struct {
    int (*crt_bundle_attach)(void *conf);
    bool non_block;
    int timeout_ms;
} esp_tls_cfg_t;
typedef struct esp_tls {
    int dummy;
} esp_tls_t;
#define ESP_TLS_ERR_SSL_WANT_READ -0x6900
int esp_crt_bundle_attach(void *conf);
esp_tls_t *esp_tls_init(void);
int esp_tls_conn_new_sync(const char *hostname, int hostlen, int port, const esp_tls_cfg_t *cfg, esp_tls_t *tls);
int esp_tls_conn_read(esp_tls_t *tls, void *data, size_t datalen);
int esp_tls_conn_write(esp_tls_t *tls, const void *data, size_t datalen);
int esp_tls_get_bytes_avail(esp_tls_t *tls);
void esp_tls_conn_destroy(esp_tls_t *tls);
esp_err_t esp_tls_get_error_handle(esp_tls_t *tls, esp_tls_error_handle_t *err_handle);
esp_err_t esp_tls_get_and_clear_last_error(esp_tls_error_handle_t h, int *esp_tls_code, int *esp_tls_flags);

// Stub settings_init
void settings_init(void);

#ifdef __cplusplus
}
#endif

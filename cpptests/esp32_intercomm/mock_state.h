#pragma once

#include "pxt.h"
#include "esp_idf_stubs.h"
#include "lwip/sockets.h"
#include <map>
#include <deque>
#include <vector>
#include <utility>

struct SentEspNowFrame {
    uint8_t peer[6];
    std::vector<uint8_t> data;
};

struct RecordedEvent {
    int id;
    int value;
};

struct MockSocket {
    int fd = -1;
    int domain = 0;
    int type = 0;
    int protocol = 0;
    bool broadcast = false;
    bool reuseaddr = false;
    bool bound = false;
    uint16_t bound_port = 0;
    bool bind_fail_once = false;
    bool closed = false;
    std::vector<std::vector<uint8_t>> sent_packets;
    std::vector<sockaddr_in> sent_dests;
    std::deque<std::vector<uint8_t>> rx_queue;
    bool ioctl_error = false;
};

struct MockState {
    // Event loop & Wi-Fi state
    bool netif_inited = false;
    bool event_loop_created = false;
    bool wifi_inited = false;
    bool wifi_started = false;
    wifi_mode_t wifi_mode = WIFI_MODE_NULL;
    int wifi_init_calls = 0;
    int event_loop_create_calls = 0;
    int8_t last_tx_power = -1;
    uint8_t last_channel = 0;

    // ESP-NOW state
    bool espnow_inited = false;
    esp_now_recv_cb_t espnow_recv_cb = nullptr;
    std::vector<esp_now_peer_info_t> espnow_peers;
    std::vector<SentEspNowFrame> sent_espnow;

    // Scan results
    std::vector<wifi_ap_record_t> scan_aps;
    std::map<int, esp_event_handler_t> wifi_handlers;
    esp_event_handler_t got_ip_handler = nullptr;

    // GPIO state
    std::map<int, int> gpio_levels;
    std::map<int, int> gpio_dirs;
    int cfg_wifi_reset = 3;
    int cfg_wifi_gpio0 = 14;

    // PXT events & fibers
    std::vector<RecordedEvent> events;
    std::vector<std::pair<int, int>> dal_registrations;
    int last_resumed_int = 0;
    Buffer last_resumed_buffer = nullptr;

    // Mock POSIX/lwIP sockets
    int next_fd = 3;
    std::map<int, MockSocket> sockets;
    bool force_bind_port_fail = false;
    bool force_getaddrinfo_fail = false;

    void reset() {
        netif_inited = false;
        event_loop_created = false;
        wifi_inited = false;
        wifi_started = false;
        wifi_mode = WIFI_MODE_NULL;
        wifi_init_calls = 0;
        event_loop_create_calls = 0;
        last_tx_power = -1;
        last_channel = 0;
        espnow_inited = false;
        espnow_recv_cb = nullptr;
        espnow_peers.clear();
        sent_espnow.clear();
        scan_aps.clear();
        wifi_handlers.clear();
        got_ip_handler = nullptr;
        gpio_levels.clear();
        gpio_dirs.clear();
        events.clear();
        dal_registrations.clear();
        last_resumed_int = 0;
        last_resumed_buffer = nullptr;
        next_fd = 3;
        sockets.clear();
        force_bind_port_fail = false;
        force_getaddrinfo_fail = false;
    }
};

extern MockState g_mock;

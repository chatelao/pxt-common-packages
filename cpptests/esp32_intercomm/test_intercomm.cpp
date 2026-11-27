#include "mock_state.h"

namespace radio {
void off();
void on();
void raiseEvent(int src, int value);
Buffer readRawPacket();
void sendRawPacket(Buffer msg);
void onDataReceived(Action body);
void setGroup(int id);
void setTransmitPower(int power);
void setFrequencyBand(int band);
} // namespace radio

namespace _wifi {
int eventID();
void scanStart();
Buffer scanResults();
void setExternalAntenna(bool external);
int socketAlloc();
int socketConnectUDP(int fd, String host, int port);
int socketWrite(int fd, Buffer data);
int socketRead(int fd, int size);
int socketBytesAvailable(int fd);
int socketClose(int fd);
} // namespace _wifi

static void check(bool cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        exit(1);
    }
}

static int32_t read_i32_le(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static void test_radio_espnow() {
    // U-RAD-01: ESP-NOW framing & broadcast peer registration
    radio::setGroup(42);
    check(g_mock.espnow_inited, "U-RAD-01: esp_now_init should be called");
    check(g_mock.espnow_peers.size() == 1, "U-RAD-01: broadcast peer should be registered");
    check(g_mock.espnow_recv_cb != nullptr, "U-RAD-01: recv callback must be registered");

    uint8_t raw[36];
    memset(raw, 0, sizeof(raw));
    for (int i = 0; i < 32; ++i) raw[i] = (uint8_t)(i + 10);
    Buffer b = mkBuffer(raw, 36);
    radio::sendRawPacket(b);
    freeBuffer(b);
    check(g_mock.sent_espnow.size() == 1, "U-RAD-01: expected 1 sent ESP-NOW frame");
    check(g_mock.sent_espnow[0].data.size() == 36, "U-RAD-01: frame length must be 4 + 32 = 36");
    check(g_mock.sent_espnow[0].data[0] == 'P' && g_mock.sent_espnow[0].data[1] == 'R', "U-RAD-01: magic 'PR'");
    check(g_mock.sent_espnow[0].data[2] == 42, "U-RAD-01: group byte must be 42");
    check(g_mock.sent_espnow[0].data[3] == 32, "U-RAD-01: payload len must be 32");
    check(memcmp(&g_mock.sent_espnow[0].data[4], raw, 32) == 0, "U-RAD-01: payload mismatch");

    // U-RAD-02: sendRawPacket bounds (NULL, <4, ==4, >36)
    g_mock.sent_espnow.clear();
    radio::sendRawPacket(nullptr);
    Buffer b2 = mkBuffer(raw, 2);
    radio::sendRawPacket(b2);
    freeBuffer(b2);
    Buffer b4 = mkBuffer(raw, 4);
    radio::sendRawPacket(b4);
    freeBuffer(b4);
    uint8_t big[40] = {0};
    Buffer b40 = mkBuffer(big, 40);
    radio::sendRawPacket(b40);
    freeBuffer(b40);
    check(g_mock.sent_espnow.empty(), "U-RAD-02: invalid buffer lengths must not send");

    // U-RAD-03: RX validation (truncated, bad magic, rp->len > len - 4, rp->len == 0, rp->len > 32)
    g_mock.events.clear();
    uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    wifi_pkt_rx_ctrl_t rx_ctrl = {-58};
    esp_now_recv_info_t info = {mac, mac, &rx_ctrl};

    uint8_t short_pkt[3] = {'P', 'R', 42};
    g_mock.espnow_recv_cb(&info, short_pkt, 3);
    uint8_t bad_magic[8] = {'X', 'R', 42, 4, 1, 2, 3, 4};
    g_mock.espnow_recv_cb(&info, bad_magic, 8);
    uint8_t zero_len[6] = {'P', 'R', 42, 0, 1, 2};
    g_mock.espnow_recv_cb(&info, zero_len, 6);
    uint8_t over_len[8] = {'P', 'R', 42, 10, 1, 2, 3, 4};
    g_mock.espnow_recv_cb(&info, over_len, 8);
    uint8_t huge_len[40] = {'P', 'R', 42, 33};
    g_mock.espnow_recv_cb(&info, huge_len, 40);
    check(radio::readRawPacket() == nullptr, "U-RAD-03: invalid packets must be dropped");
    check(g_mock.events.empty(), "U-RAD-03: no events on invalid packets");

    // U-RAD-04: Group filtering for PR and PE packets
    uint8_t wrong_grp[8] = {'P', 'R', 99, 4, 0xaa, 0xbb, 0xcc, 0xdd};
    g_mock.espnow_recv_cb(&info, wrong_grp, 8);
    check(radio::readRawPacket() == nullptr, "U-RAD-04: wrong group PR packet must be dropped");

    uint8_t wrong_evt[12] = {'P', 'E', 99, 0, 0, 0, 0, 0, 1, 0, 0, 0};
    g_mock.espnow_recv_cb(&info, wrong_evt, 12);
    check(g_mock.events.empty(), "U-RAD-04: wrong group PE packet must be dropped");

    // U-RAD-05: RSSI extraction (valid rx_ctrl and NULL info fallback -73)
    uint8_t ok_pkt[8] = {'P', 'R', 42, 4, 0x11, 0x22, 0x33, 0x44};
    g_mock.espnow_recv_cb(&info, ok_pkt, 8);
    g_mock.espnow_recv_cb(nullptr, ok_pkt, 8);
    check(g_mock.events.size() == 2, "U-RAD-05: expected 2 datagram events");
    Buffer r1 = radio::readRawPacket();
    check(r1 != nullptr && r1->length == 36, "U-RAD-05: r1 length");
    check(r1->data[0] == 0x11 && r1->data[3] == 0x44, "U-RAD-05: r1 payload");
    check(read_i32_le(r1->data + 32) == -58, "U-RAD-05: expected RSSI -58");
    freeBuffer(r1);

    Buffer r2 = radio::readRawPacket();
    check(r2 != nullptr && read_i32_le(r2->data + 32) == -73, "U-RAD-05: expected fallback RSSI -73");
    freeBuffer(r2);
    check(radio::readRawPacket() == nullptr, "U-RAD-05: queue should now be empty");

    // U-RAD-06: RX queue saturation (16 items capacity)
    g_mock.events.clear();
    for (int i = 0; i < 20; ++i) {
        ok_pkt[4] = (uint8_t)i;
        g_mock.espnow_recv_cb(&info, ok_pkt, 8);
    }
    check(g_mock.events.size() == 16, "U-RAD-06: only 16 events should be queued");
    for (int i = 0; i < 16; ++i) {
        Buffer r = radio::readRawPacket();
        check(r != nullptr && r->data[0] == (uint8_t)i, "U-RAD-06: FIFO order mismatch");
        freeBuffer(r);
    }
    check(radio::readRawPacket() == nullptr, "U-RAD-06: remaining 4 must have dropped");

    // U-RAD-07: off() / on() lifecycle
    g_mock.sent_espnow.clear();
    g_mock.events.clear();
    radio::off();
    g_mock.espnow_recv_cb(&info, ok_pkt, 8);
    Buffer b_off = mkBuffer(raw, 36);
    radio::sendRawPacket(b_off);
    freeBuffer(b_off);
    check(g_mock.sent_espnow.empty(), "U-RAD-07: sendRawPacket must not send while off");
    check(g_mock.events.empty(), "U-RAD-07: recv_cb must drop while off");
    check(radio::readRawPacket() == nullptr, "U-RAD-07: readRawPacket must return NULL while off");

    radio::on();
    check(g_mock.espnow_peers.size() == 1, "U-RAD-07: peer must not be duplicated on radio::on");
    g_mock.espnow_recv_cb(&info, ok_pkt, 8);
    check(g_mock.events.size() == 1, "U-RAD-07: recv must resume after radio::on");
    Buffer r_on = radio::readRawPacket();
    check(r_on != nullptr, "U-RAD-07: packet readable after radio::on");
    freeBuffer(r_on);

    // U-RAD-08: Power & frequency band clamping/mapping
    radio::setTransmitPower(-5);
    check(g_mock.last_tx_power == 8, "U-RAD-08: power -5 clamps to 0 -> 8");
    radio::setTransmitPower(99);
    check(g_mock.last_tx_power == 78, "U-RAD-08: power 99 clamps to 7 -> 78");
    radio::setFrequencyBand(-5);
    check(g_mock.last_channel == 1, "U-RAD-08: band -5 -> ch 1");
    radio::setFrequencyBand(17);
    check(g_mock.last_channel == 2, "U-RAD-08: band 17 -> ch 2");
    radio::setFrequencyBand(72);
    check(g_mock.last_channel == 13, "U-RAD-08: band 72 -> ch 13");
    radio::setFrequencyBand(200);
    check(g_mock.last_channel == 13, "U-RAD-08: band 200 -> ch 13");

    // U-RAD-09: raiseEvent roundtrip
    g_mock.sent_espnow.clear();
    g_mock.events.clear();
    radio::raiseEvent(2000, 777);
    check(g_mock.sent_espnow.size() == 1, "U-RAD-09: raiseEvent must transmit 1 frame");
    const auto &ef = g_mock.sent_espnow[0].data;
    g_mock.espnow_recv_cb(&info, ef.data(), (int)ef.size());
    check(g_mock.events.size() == 1 && g_mock.events[0].id == 2000 && g_mock.events[0].value == 777,
          "U-RAD-09: event roundtrip failed");

    printf("✔ U-RAD-01..09 (ESP-NOW radio unit tests) passed\n");
}

static void test_wifi_coex_and_80211ax() {
    // U-WIF-01: _wifi::init() called AFTER radioEnable() already initialized Wi-Fi & event loop
    int prev_wifi_inits = g_mock.wifi_init_calls;
    _wifi::scanStart();
    check(g_mock.wifi_init_calls == prev_wifi_inits, "U-WIF-01: _wifi::init() must reuse already-initialized Wi-Fi");
    check(g_mock.wifi_handlers.count(WIFI_EVENT_SCAN_DONE) == 1, "U-WIF-01: scan_done handler registered");

    // U-WIF-02: scanResults() 802.11ax flag check
    wifi_ap_record_t ap_ax = {};
    strcpy((char *)ap_ax.ssid, "Wifi6Router");
    ap_ax.primary = 6;
    ap_ax.rssi = -42;
    ap_ax.phy_11ax = 1;
    ap_ax.authmode = WIFI_AUTH_WPA2_PSK;

    wifi_ap_record_t ap_n = {};
    strcpy((char *)ap_n.ssid, "LegacyRouter");
    ap_n.primary = 1;
    ap_n.rssi = -65;
    ap_n.phy_11n = 1;
    ap_n.authmode = WIFI_AUTH_OPEN;

    g_mock.scan_aps = {ap_ax, ap_n};
    g_mock.wifi_handlers[WIFI_EVENT_SCAN_DONE](nullptr, WIFI_EVENT, WIFI_EVENT_SCAN_DONE, nullptr);

    Buffer res = _wifi::scanResults();
    check(res != nullptr && res->length == 96, "U-WIF-02: expected 2 * 48 = 96 bytes");
    uint32_t flags0 = (uint32_t)read_i32_le(res->data);
    uint32_t flags1 = (uint32_t)read_i32_le(res->data + 48);
    check((flags0 & 0x2000) != 0, "U-WIF-02: AP0 must have IEEE_802_11AX (0x2000)");
    check((flags0 & 0x1) != 0, "U-WIF-02: AP0 must have HAS_PASSWORD (0x1)");
    check((flags1 & 0x2000) == 0, "U-WIF-02: AP1 must not have IEEE_802_11AX");
    check((flags1 & 0x800) != 0, "U-WIF-02: AP1 must have IEEE_802_11N");
    freeBuffer(res);

    // Antenna switch GPIO3 / GPIO14 check
    _wifi::setExternalAntenna(true);
    check(g_mock.gpio_levels[3] == 0 && g_mock.gpio_levels[14] == 1, "Antenna external=true sets GPIO3=0, GPIO14=1");
    _wifi::setExternalAntenna(false);
    check(g_mock.gpio_levels[3] == 0 && g_mock.gpio_levels[14] == 0, "Antenna external=false sets GPIO3=0, GPIO14=0");

    printf("✔ U-WIF-01..02 (Wi-Fi coexistence, 802.11ax scan flag, antenna switch) passed\n");
}

static void test_udp_sockets() {
    // U-UDP-01: Invalid fd and port validation
    String bcastHost = mkString("255.255.255.255");
    check(_wifi::socketConnectUDP(0, bcastHost, 12345) == -10, "U-UDP-01: fd 0 must return -10");
    check(_wifi::socketConnectUDP(16, bcastHost, 12345) == -10, "U-UDP-01: fd 16 must return -10");
    check(_wifi::socketConnectUDP(1, bcastHost, 12345) == -11, "U-UDP-01: unallocated fd must return -11");

    int fd = _wifi::socketAlloc();
    check(fd == 1, "U-UDP-01: first allocated fd must be 1");
    check(_wifi::socketConnectUDP(fd, bcastHost, 0) == -3, "U-UDP-01: port 0 must return -3");
    check(_wifi::socketConnectUDP(fd, bcastHost, 70000) == -3, "U-UDP-01: port > 65535 must return -3");

    // U-UDP-02: Broadcast connect, sendto, bind 0.0.0.0, fallback bind, rx read
    _wifi::socketConnectUDP(fd, bcastHost, 12345);
    check(g_mock.last_resumed_int == 1, "U-UDP-02: socketConnectUDP should resume with 1");
    check(_wifi::socketConnectUDP(fd, bcastHost, 12345) == -2, "U-UDP-02: double connect must return -2");

    int raw_fd = g_mock.next_fd - 1;
    const MockSocket &ms = g_mock.sockets[raw_fd];
    check(ms.type == SOCK_DGRAM && ms.protocol == IPPROTO_UDP, "U-UDP-02: must create SOCK_DGRAM");
    check(ms.broadcast && ms.reuseaddr, "U-UDP-02: SO_BROADCAST & SO_REUSEADDR must be enabled");
    check(ms.bound && ms.bound_port == 12345, "U-UDP-02: must bind requested port");

    Buffer payload = mkBuffer("ping", 4);
    _wifi::socketWrite(fd, payload);
    freeBuffer(payload);
    check(g_mock.last_resumed_int == 4, "U-UDP-02: write must resume with 4 bytes");
    check(g_mock.sockets[raw_fd].sent_dests.size() == 1, "U-UDP-02: must use sendto for destination");
    check(ntohl(g_mock.sockets[raw_fd].sent_dests[0].sin_addr.s_addr) == 0xFFFFFFFFu,
          "U-UDP-02: destination IP must be 255.255.255.255");
    check(ntohs(g_mock.sockets[raw_fd].sent_dests[0].sin_port) == 12345,
          "U-UDP-02: destination port must be 12345");

    // Queue an incoming UDP packet and read it via socketRead
    g_mock.sockets[raw_fd].rx_queue.push_back({'p', 'o', 'n', 'g'});
    _wifi::socketRead(fd, 16);
    check(g_mock.last_resumed_buffer != nullptr && g_mock.last_resumed_buffer->length == 4,
          "U-UDP-02: socketRead must return 4-byte buffer");
    check(memcmp(g_mock.last_resumed_buffer->data, "pong", 4) == 0, "U-UDP-02: RX payload mismatch");
    freeBuffer(g_mock.last_resumed_buffer);
    g_mock.last_resumed_buffer = nullptr;

    check(_wifi::socketClose(fd) == 0, "U-UDP-02: socketClose must succeed");
    check(g_mock.sockets[raw_fd].closed, "U-UDP-02: underlying lwIP fd must be closed");

    // 0.0.0.0 bind-only + port collision fallback to port 0
    g_mock.force_bind_port_fail = true;
    int rx_fd = _wifi::socketAlloc();
    String anyHost = mkString("0.0.0.0");
    _wifi::socketConnectUDP(rx_fd, anyHost, 12345);
    int raw_rx = g_mock.next_fd - 1;
    check(g_mock.sockets[raw_rx].bound && g_mock.sockets[raw_rx].bound_port == 0,
          "U-UDP-02: bind fallback to ephemeral port 0");
    _wifi::socketClose(rx_fd);
    g_mock.force_bind_port_fail = false;

    // U-UDP-03: Socket pool exhaustion (MAX_SOCKET=16 -> slots 1..15) & reclamation
    std::vector<int> fds;
    for (int i = 0; i < 15; ++i) {
        int s = _wifi::socketAlloc();
        check(s == i + 1, "U-UDP-03: expected sequential fd allocation");
        fds.push_back(s);
    }
    check(_wifi::socketAlloc() == -1, "U-UDP-03: 16th alloc must return -1");
    _wifi::socketClose(fds[5]);
    check(_wifi::socketAlloc() == 6, "U-UDP-03: closed slot 6 must be reusable");
    fds[5] = 6;
    for (int s : fds) _wifi::socketClose(s);

    delete bcastHost;
    delete anyHost;
    printf("✔ U-UDP-01..03 (UDP socket validation, broadcast, bind fallback, pool reuse) passed\n");
}

int main() {
    g_mock.reset();
    test_radio_espnow();
    test_wifi_coex_and_80211ax();
    test_udp_sockets();
    printf("ALL C++ UNIT TESTS PASSED!\n");
    return 0;
}

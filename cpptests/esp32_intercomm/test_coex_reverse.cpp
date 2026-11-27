#include "mock_state.h"

namespace radio {
void setGroup(int id);
void sendRawPacket(Buffer msg);
} // namespace radio

namespace _wifi {
void scanStart();
} // namespace _wifi

static void check(bool cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        exit(1);
    }
}

int main() {
    g_mock.reset();
    // Reverse order: _wifi::init() runs FIRST, then radioEnable() runs AFTER
    _wifi::scanStart();
    check(g_mock.wifi_init_calls == 1, "reverse-coex: _wifi::init() must initialize Wi-Fi once");
    check(g_mock.event_loop_created, "reverse-coex: event loop must be created");

    radio::setGroup(7);
    check(g_mock.wifi_init_calls == 1, "reverse-coex: radioEnable() must reuse Wi-Fi without re-init");
    check(g_mock.espnow_inited, "reverse-coex: ESP-NOW must initialize on top of existing Wi-Fi");
    check(g_mock.espnow_peers.size() == 1, "reverse-coex: broadcast peer must be added");

    uint8_t raw[36] = {1, 2, 3, 4};
    Buffer b = mkBuffer(raw, 36);
    radio::sendRawPacket(b);
    freeBuffer(b);
    check(g_mock.sent_espnow.size() == 1, "reverse-coex: ESP-NOW send must succeed after Wi-Fi init");

    printf("✔ Reverse Wi-Fi -> ESP-NOW coexistence initialization order passed\n");
    return 0;
}

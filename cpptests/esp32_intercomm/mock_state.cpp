#include "mock_state.h"
#include <stdarg.h>

MockState g_mock;

esp_event_base_t WIFI_EVENT = "WIFI_EVENT";
esp_event_base_t IP_EVENT = "IP_EVENT";

struct MockQueue {
    uint32_t capacity;
    uint32_t item_size;
    std::deque<std::vector<uint8_t>> items;
};

namespace pxt {

void raiseEvent(int id, int event) {
    g_mock.events.push_back({id, event});
}

void registerWithDal(int id, int event, Action a, int flags) {
    (void)a;
    (void)flags;
    g_mock.dal_registrations.push_back({id, event});
}

Buffer mkBuffer(const void *data, int len) {
    if (len < 0) len = 0;
    auto b = (BoxedBuffer *)calloc(1, sizeof(BoxedBuffer) + (size_t)len);
    b->length = (uint32_t)len;
    if (data && len > 0)
        memcpy(b->data, data, (size_t)len);
    return b;
}

void freeBuffer(Buffer b) {
    free(b);
}

String mkString(const char *s, int len) {
    auto res = new BoxedString();
    if (s) {
        if (len >= 0) res->str.assign(s, (size_t)len);
        else res->str.assign(s);
    }
    return res;
}

TValue fromInt(int v) {
    return (TValue)v;
}

int getConfig(int key, int defl) {
    if (key == CFG_PIN_WIFI_RESET) return g_mock.cfg_wifi_reset;
    if (key == CFG_PIN_WIFI_GPIO0) return g_mock.cfg_wifi_gpio0;
    return defl;
}

void memInfo() {}
void vm_stack_trace() {}

struct worker {
    TaskFunction_t idle_fn = nullptr;
    void *idle_arg = nullptr;
};

worker_t worker_alloc(const char *id, uint32_t stack_size) {
    (void)id;
    (void)stack_size;
    return new worker();
}

void worker_set_idle(worker_t w, TaskFunction_t fn, void *arg) {
    if (w) {
        w->idle_fn = fn;
        w->idle_arg = arg;
    }
}

int worker_run(worker_t w, TaskFunction_t fn, void *arg) {
    (void)w;
    if (fn) fn(arg);
    return 0;
}

FiberContext *suspendFiber() {
    static FiberContext ctx{1};
    return &ctx;
}

void resumeFiber(FiberContext *ctx, TValue v) {
    (void)ctx;
    g_mock.last_resumed_int = (int)v;
}

void resumeFiberWithFn(FiberContext *ctx, fiber_resume_t fn, void *arg) {
    (void)ctx;
    g_mock.last_resumed_buffer = fn(arg);
}

} // namespace pxt

namespace _wifi {
void startHttpServer(const char *hostName) {
    (void)hostName;
}
} // namespace _wifi

extern "C" {

void target_panic(int error_code) {
    fprintf(stderr, "target_panic(%d)\n", error_code);
    abort();
}

QueueHandle_t xQueueCreate(uint32_t uxQueueLength, uint32_t uxItemSize) {
    auto q = new MockQueue();
    q->capacity = uxQueueLength;
    q->item_size = uxItemSize;
    return (QueueHandle_t)q;
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait) {
    (void)xTicksToWait;
    auto q = (MockQueue *)xQueue;
    if (!q || q->items.size() >= q->capacity)
        return pdFAIL;
    const uint8_t *p = (const uint8_t *)pvItemToQueue;
    q->items.emplace_back(p, p + q->item_size);
    return pdPASS;
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait) {
    (void)xTicksToWait;
    auto q = (MockQueue *)xQueue;
    if (!q || q->items.empty())
        return pdFAIL;
    memcpy(pvBuffer, q->items.front().data(), q->item_size);
    q->items.pop_front();
    return pdPASS;
}

void vTaskDelay(TickType_t xTicksToDelay) {
    (void)xTicksToDelay;
}

esp_err_t esp_netif_init(void) {
    g_mock.netif_inited = true;
    return ESP_OK;
}

esp_err_t esp_event_loop_create_default(void) {
    g_mock.event_loop_create_calls++;
    if (g_mock.event_loop_created)
        return ESP_ERR_INVALID_STATE;
    g_mock.event_loop_created = true;
    return ESP_OK;
}

esp_err_t esp_wifi_get_mode(wifi_mode_t *mode) {
    if (!g_mock.wifi_inited)
        return ESP_ERR_WIFI_NOT_INIT;
    if (mode) *mode = g_mock.wifi_mode;
    return ESP_OK;
}

esp_err_t esp_wifi_init(const wifi_init_config_t *config) {
    (void)config;
    g_mock.wifi_init_calls++;
    g_mock.wifi_inited = true;
    return ESP_OK;
}

esp_err_t esp_wifi_set_storage(wifi_storage_t storage) {
    (void)storage;
    return ESP_OK;
}

esp_err_t esp_wifi_set_mode(wifi_mode_t mode) {
    g_mock.wifi_mode = mode;
    return ESP_OK;
}

esp_err_t esp_wifi_start(void) {
    g_mock.wifi_started = true;
    return ESP_OK;
}

esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block) {
    (void)config;
    (void)block;
    return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number) {
    if (number) *number = (uint16_t)g_mock.scan_aps.size();
    return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *ap_records) {
    uint16_t n = number ? *number : 0;
    for (uint16_t i = 0; i < n && i < g_mock.scan_aps.size(); ++i)
        ap_records[i] = g_mock.scan_aps[i];
    return ESP_OK;
}

esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf) {
    (void)interface;
    (void)conf;
    return ESP_OK;
}

esp_err_t esp_wifi_connect(void) { return ESP_OK; }
esp_err_t esp_wifi_disconnect(void) { return ESP_OK; }

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info) {
    (void)ap_info;
    return ESP_FAIL;
}

esp_err_t esp_wifi_set_max_tx_power(int8_t power) {
    g_mock.last_tx_power = power;
    return ESP_OK;
}

esp_err_t esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t second) {
    (void)second;
    g_mock.last_channel = primary;
    return ESP_OK;
}

esp_netif_t *esp_netif_new(const esp_netif_config_t *esp_netif_config) {
    (void)esp_netif_config;
    static int dummy_netif = 1;
    return (esp_netif_t *)&dummy_netif;
}

esp_netif_t *esp_netif_create_default_wifi_ap(void) {
    static int dummy_ap_netif = 2;
    return (esp_netif_t *)&dummy_ap_netif;
}

esp_err_t esp_netif_get_ip_info(esp_netif_t *esp_netif, esp_netif_ip_info_t *ip_info) {
    (void)esp_netif;
    if (ip_info) {
        memset(ip_info, 0, sizeof(*ip_info));
        ip_info->ip.addr = 0x0104A8C0; // 192.168.4.1
        ip_info->netmask.addr = 0x00FFFFFF; // 255.255.255.0
        ip_info->gw.addr = 0x0104A8C0;
    }
    return ESP_OK;
}

esp_err_t esp_netif_attach_wifi_station(esp_netif_t *esp_netif) {
    (void)esp_netif;
    return ESP_OK;
}

esp_err_t esp_wifi_set_default_wifi_sta_handlers(void) {
    return ESP_OK;
}

esp_err_t esp_event_handler_register(esp_event_base_t event_base, int32_t event_id,
                                     esp_event_handler_t event_handler, void *event_handler_arg) {
    (void)event_handler_arg;
    if (event_base == WIFI_EVENT)
        g_mock.wifi_handlers[event_id] = event_handler;
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
        g_mock.got_ip_handler = event_handler;
    return ESP_OK;
}

esp_err_t esp_now_init(void) {
    if (g_mock.espnow_inited) return ESP_ERR_ESPNOW_EXIST;
    g_mock.espnow_inited = true;
    return ESP_OK;
}

esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb) {
    g_mock.espnow_recv_cb = cb;
    return ESP_OK;
}

bool esp_now_is_peer_exist(const uint8_t *peer_addr) {
    for (const auto &p : g_mock.espnow_peers) {
        if (memcmp(p.peer_addr, peer_addr, 6) == 0)
            return true;
    }
    return false;
}

esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer) {
    if (peer) g_mock.espnow_peers.push_back(*peer);
    return ESP_OK;
}

esp_err_t esp_now_send(const uint8_t *peer_addr, const uint8_t *data, size_t len) {
    SentEspNowFrame f;
    memcpy(f.peer, peer_addr, 6);
    f.data.assign(data, data + len);
    g_mock.sent_espnow.push_back(std::move(f));
    return ESP_OK;
}

esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode) {
    g_mock.gpio_dirs[gpio_num] = mode;
    return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level) {
    g_mock.gpio_levels[gpio_num] = (int)level;
    return ESP_OK;
}

void esp_log_level_set(const char *tag, esp_log_level_t level) {
    (void)tag;
    (void)level;
}

int esp_crt_bundle_attach(void *conf) { (void)conf; return 0; }
esp_tls_t *esp_tls_init(void) { return new esp_tls_t(); }
int esp_tls_conn_new_sync(const char *, int, int, const esp_tls_cfg_t *, esp_tls_t *) { return -1; }
int esp_tls_conn_read(esp_tls_t *, void *, size_t) { return 0; }
int esp_tls_conn_write(esp_tls_t *, const void *, size_t) { return -1; }
int esp_tls_get_bytes_avail(esp_tls_t *) { return 0; }
void esp_tls_conn_destroy(esp_tls_t *t) { delete t; }
esp_err_t esp_tls_get_error_handle(esp_tls_t *, esp_tls_error_handle_t *) { return ESP_OK; }
esp_err_t esp_tls_get_and_clear_last_error(esp_tls_error_handle_t, int *, int *) { return ESP_OK; }

void settings_init(void) {}

// lwIP / POSIX socket mocks
int mock_socket(int domain, int type, int protocol) {
    int fd = g_mock.next_fd++;
    MockSocket s;
    s.fd = fd;
    s.domain = domain;
    s.type = type;
    s.protocol = protocol;
    g_mock.sockets[fd] = s;
    return fd;
}

int mock_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
    (void)level;
    (void)optlen;
    auto it = g_mock.sockets.find(sockfd);
    if (it == g_mock.sockets.end()) return -1;
    int val = optval ? *(const int *)optval : 0;
    if (optname == SO_BROADCAST) it->second.broadcast = (val != 0);
    if (optname == SO_REUSEADDR) it->second.reuseaddr = (val != 0);
    return 0;
}

int mock_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    (void)addrlen;
    auto it = g_mock.sockets.find(sockfd);
    if (it == g_mock.sockets.end()) return -1;
    const auto *in = (const struct sockaddr_in *)addr;
    uint16_t port = ntohs(in->sin_port);
    if (g_mock.force_bind_port_fail && port != 0)
        return -1;
    it->second.bound = true;
    it->second.bound_port = port;
    return 0;
}

int mock_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    return 0;
}

int mock_send(int sockfd, const void *buf, size_t len, int flags) {
    (void)flags;
    auto it = g_mock.sockets.find(sockfd);
    if (it == g_mock.sockets.end()) return -1;
    const uint8_t *p = (const uint8_t *)buf;
    it->second.sent_packets.emplace_back(p, p + len);
    return (int)len;
}

int mock_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen) {
    (void)flags;
    (void)addrlen;
    auto it = g_mock.sockets.find(sockfd);
    if (it == g_mock.sockets.end()) return -1;
    const uint8_t *p = (const uint8_t *)buf;
    it->second.sent_packets.emplace_back(p, p + len);
    it->second.sent_dests.push_back(*(const struct sockaddr_in *)dest_addr);
    return (int)len;
}

int mock_recv(int sockfd, void *buf, size_t len, int flags) {
    (void)flags;
    auto it = g_mock.sockets.find(sockfd);
    if (it == g_mock.sockets.end() || it->second.rx_queue.empty()) return -1;
    auto &pkt = it->second.rx_queue.front();
    size_t n = pkt.size() < len ? pkt.size() : len;
    memcpy(buf, pkt.data(), n);
    it->second.rx_queue.pop_front();
    return (int)n;
}

int mock_close(int fd) {
    auto it = g_mock.sockets.find(fd);
    if (it == g_mock.sockets.end()) return -1;
    it->second.closed = true;
    return 0;
}

int mock_ioctl(int fd, unsigned long req, ...) {
    auto it = g_mock.sockets.find(fd);
    if (it == g_mock.sockets.end() || it->second.ioctl_error) return -1;
    if (req == FIONREAD) {
        va_list ap;
        va_start(ap, req);
        int *out = va_arg(ap, int *);
        va_end(ap);
        *out = it->second.rx_queue.empty() ? 0 : (int)it->second.rx_queue.front().size();
    }
    return 0;
}

static uint32_t parse_ipv4(const char *s) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (s && sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        return htonl((a << 24) | (b << 16) | (c << 8) | d);
    }
    return htonl(0x7f000001);
}

int mock_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res) {
    (void)hints;
    if (g_mock.force_getaddrinfo_fail) return -1;
    auto *ai = (struct addrinfo *)calloc(1, sizeof(struct addrinfo));
    auto *sa = (struct sockaddr_in *)calloc(1, sizeof(struct sockaddr_in));
    sa->sin_family = AF_INET;
    sa->sin_port = htons((uint16_t)atoi(service ? service : "0"));
    sa->sin_addr.s_addr = parse_ipv4(node);
    ai->ai_family = AF_INET;
    ai->ai_socktype = hints ? hints->ai_socktype : SOCK_DGRAM;
    ai->ai_addrlen = sizeof(struct sockaddr_in);
    ai->ai_addr = (struct sockaddr *)sa;
    *res = ai;
    return 0;
}

void mock_freeaddrinfo(struct addrinfo *res) {
    if (!res) return;
    free(res->ai_addr);
    free(res);
}

} // extern "C"

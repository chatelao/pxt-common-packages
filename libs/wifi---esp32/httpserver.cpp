#include "wifi.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include <esp_http_server.h>
#include <mdns.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <string>

#define TAG "http"
#define LOG(...) ESP_LOGI(TAG, __VA_ARGS__)

namespace _wifi {

enum class ServerMode {
    None = 0,
    Login = 1,
    Dashboard = 2,
};

enum WidgetKind {
    WIDGET_COLOR = 1,
    WIDGET_BUTTON = 2,
    WIDGET_SLIDER = 3,
    WIDGET_STATUS = 4,
};

#define MAX_WIDGETS 16

struct DashboardWidget {
    bool active;
    int32_t id;
    int32_t kind;
    char label[48];
    int32_t minVal;
    int32_t maxVal;
    int32_t value;
    char text[64];
    bool pendingValue;
    int32_t pendingClicks;
};

static httpd_handle_t _server = NULL;
static ServerMode _serverMode = ServerMode::None;
static const char* _lastApBuffer = NULL;
static bool _mdnsInitialized = false;

static SemaphoreHandle_t _dashMutex = NULL;
static DashboardWidget _widgets[MAX_WIDGETS];
static int _widgetCount = 0;
static char _dashTitle[48] = "MakeCode Control";
static bool _captivePortalEnabled = true;

static volatile bool _dnsRunning = false;
static TaskHandle_t _dnsTask = NULL;
static int _dnsSock = -1;

static void ensureDashMutex() {
    if (!_dashMutex) {
        _dashMutex = xSemaphoreCreateMutex();
    }
}

static void initMdnsOnce(const char* hostName) {
    if (!_mdnsInitialized) {
        if (mdns_init() != ESP_OK)
            return;
        _mdnsInitialized = true;
        mdns_hostname_set(hostName);
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    } else {
        mdns_hostname_set(hostName);
    }
}

static uint32_t getApIpNetworkOrder() {
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap && esp_netif_get_ip_info(ap, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        return ip_info.ip.addr;
    }
    return htonl(0xC0A80401); // 192.168.4.1
}

static void formatApBaseUrl(char* out, size_t outLen) {
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap && esp_netif_get_ip_info(ap, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        snprintf(out, outLen, "http://" IPSTR "/", IP2STR(&ip_info.ip));
    } else {
        snprintf(out, outLen, "http://192.168.4.1/");
    }
}

static void captive_dns_task(void* arg) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG("captive DNS socket failed");
        _dnsRunning = false;
        _dnsTask = NULL;
        vTaskDelete(NULL);
        return;
    }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG("captive DNS bind failed");
        close(sock);
        _dnsRunning = false;
        _dnsTask = NULL;
        vTaskDelete(NULL);
        return;
    }

    _dnsSock = sock;
    LOG("captive DNS started on :53");

    uint8_t rx[512];
    uint8_t tx[512];

    while (_dnsRunning) {
        struct sockaddr_in client;
        socklen_t clientLen = sizeof(client);
        int len = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr*)&client, &clientLen);
        if (len < 12)
            continue;

        // Only respond to standard DNS queries (QR == 0) with >= 1 question
        if ((rx[2] & 0x80) != 0)
            continue;
        uint16_t qdcount = (uint16_t)((rx[4] << 8) | rx[5]);
        if (qdcount == 0)
            continue;

        // Walk QNAME
        int pos = 12;
        while (pos < len) {
            uint8_t labelLen = rx[pos];
            if (labelLen == 0) {
                pos++;
                break;
            }
            if ((labelLen & 0xC0) == 0xC0) {
                pos += 2;
                break;
            }
            pos += 1 + labelLen;
        }
        if (pos + 4 > len || pos + 4 + 16 > (int)sizeof(tx))
            continue;

        uint16_t qtype = (uint16_t)((rx[pos] << 8) | rx[pos + 1]);
        int qend = pos + 4;

        memcpy(tx, rx, qend);
        tx[2] = (uint8_t)(0x84 | (rx[2] & 0x01)); // QR=1, AA=1, preserve RD
        tx[3] = 0x00;                             // RCODE=0
        tx[4] = 0x00;
        tx[5] = 0x01; // QDCOUNT=1
        tx[8] = 0x00;
        tx[9] = 0x00; // NSCOUNT=0
        tx[10] = 0x00;
        tx[11] = 0x00; // ARCOUNT=0

        int txLen = qend;
        if (qtype == 1 || qtype == 255) { // A or ANY -> answer with SoftAP IP
            tx[6] = 0x00;
            tx[7] = 0x01; // ANCOUNT=1
            uint32_t ipNet = getApIpNetworkOrder();
            uint8_t ans[16] = {
                0xC0, 0x0C,             // Name pointer to offset 12
                0x00, 0x01,             // TYPE A
                0x00, 0x01,             // CLASS IN
                0x00, 0x00, 0x00, 0x3C, // TTL 60s
                0x00, 0x04,             // RDLENGTH 4
                0, 0, 0, 0
            };
            memcpy(&ans[12], &ipNet, 4);
            memcpy(tx + txLen, ans, 16);
            txLen += 16;
        } else {
            // Empty NOERROR response for AAAA/HTTPS queries avoids OS DNS timeout stalls
            tx[6] = 0x00;
            tx[7] = 0x00;
        }

        sendto(sock, tx, txLen, 0, (struct sockaddr*)&client, clientLen);
    }

    close(sock);
    _dnsSock = -1;
    _dnsTask = NULL;
    vTaskDelete(NULL);
}

static void startCaptiveDns() {
    if (_dnsRunning || _dnsTask != NULL)
        return;
    _dnsRunning = true;
    if (xTaskCreate(captive_dns_task, "captive_dns", 3072, NULL, 3, &_dnsTask) != pdPASS) {
        _dnsRunning = false;
        _dnsTask = NULL;
    }
}

static void stopCaptiveDns() {
    _dnsRunning = false;
    if (_dnsSock >= 0) {
        close(_dnsSock);
        _dnsSock = -1;
    }
    for (int i = 0; i < 20 && _dnsTask != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void stopActiveServer() {
    stopCaptiveDns();
    if (_server != NULL) {
        httpd_stop(_server);
        _server = NULL;
    }
    _serverMode = ServerMode::None;
}

// ---------- Login Captive Portal Handlers ----------

esp_err_t login_handler(httpd_req_t *req)
{
    LOG("login");
    const char resp[] = "<style>html{background:#aaa}*{font-size:xx-large;font-family:monospace}form{min-height:100vh;display:flex;justify-content:center;align-items:center;flex-direction:column;gap:.5rem}</style><meta name='viewport'content='width=device-width,initial-scale=1'><form action='/add-ap'><label for='ssid'>WiFi:</label> <input name='name'id='ssid'placeholder='WiFi'required> <label for='password'>Password:</label> <input type='password'name='password'id='password'placeholder='Password'required> <input id='submit'type='submit'value='connect'></form>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t add_ap_handler(httpd_req_t *req)
{
    LOG("add_ap");

    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1 && buf_len < 256) {
        char* buf = (char*)malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char name[64];
            char password[64];
            if ((httpd_query_key_value(buf, "name", name, sizeof(name)) == ESP_OK) &&
                (httpd_query_key_value(buf, "password", password, sizeof(password)) == ESP_OK)) {
                if (NULL == _lastApBuffer) {
                    _lastApBuffer = buf;
                    pxt::raiseEvent(_wifi::eventID(), (int)_wifi::WifiEvent::AccessPointCredentialsAvailable);
                    const char resp[] = "Restarting...";
                    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
                    return ESP_OK;
                }
            }
        }
        free(buf);
        return ESP_OK;
    }

    httpd_resp_send_500(req);
    return ESP_OK;
}

//%
RefCollection *_readLastAccessPointCredentials() {
    auto res = Array_::mk();
    registerGCObj(res);

    char name[64];
    char password[64];
    if (NULL != _lastApBuffer &&
        (httpd_query_key_value(_lastApBuffer, "name", name, sizeof(name)) == ESP_OK) &&
        (httpd_query_key_value(_lastApBuffer, "password", password, sizeof(password)) == ESP_OK)) {

        {
            auto str = mkString(name, -1);
            registerGCObj(str);
            res->head.push((TValue)str);
            unregisterGCObj(str);
        }
        {
            auto str = mkString(password, -1);
            registerGCObj(str);
            res->head.push((TValue)str);
            unregisterGCObj(str);
        }
    }

    unregisterGCObj(res);
    return res;
}

// HTTP Error (404) Handler - Redirects unknown/captive-probe requests to root
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (_serverMode == ServerMode::Dashboard && !_captivePortalEnabled) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_OK;
    }
    char loc[48];
    formatApBaseUrl(loc, sizeof(loc));
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", loc);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    // iOS CNA requires non-empty content in the response to open the captive portal sheet
    httpd_resp_send(req, "<!doctype html><html><body><a href='/'>Open Control</a></body></html>", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

httpd_uri_t login_get = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = login_handler,
    .user_ctx = NULL
};

httpd_uri_t add_ap_get = {
    .uri      = "/add-ap",
    .method   = HTTP_GET,
    .handler  = add_ap_handler,
    .user_ctx = NULL
};

static void initLoginServer(const char* hostName)
{
    LOG("starting login server %s", hostName);
    if (_wifi::startAPInternal(hostName, "", 11, 1) != 0) {
        LOG("failed to start SoftAP for login server");
        return;
    }

    stopActiveServer();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    httpd_register_uri_handler(server, &login_get);
    httpd_register_uri_handler(server, &add_ap_get);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);
    LOG("SoftAP ip: " IPSTR, IP2STR(&ip_info.ip));

    initMdnsOnce(hostName);

    _server = server;
    _serverMode = ServerMode::Login;
}

void startHttpServer(const char* hostName) {
    if (_serverMode != ServerMode::Login) {
        initLoginServer(hostName);
    }
}

// ---------- Web Dashboard Handlers ----------

static const char DASHBOARD_HTML[] = R"raw(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>MakeCode Control</title>
<style>
:root{--bg:#101521;--card:#1b2234;--bd:#2d3752;--tx:#f1f5f9;--mut:#94a3b8;--acc:#14b8a6}
*{box-sizing:border-box;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;-webkit-tap-highlight-color:transparent}
body{margin:0;padding:16px;background:var(--bg);color:var(--tx);min-height:100vh}
.wrap{max-width:480px;margin:0 auto}
header{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px}
h1{font-size:1.25rem;margin:0;font-weight:700}
.dot{display:inline-flex;align-items:center;gap:6px;font-size:.8rem;color:var(--mut)}
.dot i{width:9px;height:9px;border-radius:50%;background:#22c55e;display:inline-block}
.dot.off i{background:#ef4444}
.grid{display:flex;flex-direction:column;gap:12px}
.card{background:var(--card);border:1px solid var(--bd);border-radius:14px;padding:14px 16px}
.row{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:10px}
.lbl{font-weight:600;font-size:.98rem}
.val{font-family:ui-monospace,monospace;font-size:.9rem;background:#0f1420;padding:3px 9px;border-radius:999px;border:1px solid var(--bd);color:#cbd5e1}
input[type=range]{width:100%;height:34px;accent-color:var(--acc);cursor:pointer;margin:0}
.cwrap{display:flex;align-items:center;gap:12px}
input[type=color]{width:56px;height:44px;border:0;background:none;padding:0;cursor:pointer;border-radius:10px}
.sws{display:flex;flex-wrap:wrap;gap:8px;flex:1}
.sw{width:30px;height:30px;border-radius:50%;border:2px solid rgba(255,255,255,.22);cursor:pointer;padding:0}
.sw:active{transform:scale(.9)}
.btn{width:100%;min-height:48px;border:0;border-radius:12px;background:var(--acc);color:#042f2e;font-weight:700;font-size:1rem;cursor:pointer;transition:transform .08s,opacity .1s}
.btn:active{transform:scale(.97);opacity:.85}
.st{display:flex;align-items:center;justify-content:space-between;gap:12px}
.empty{text-align:center;color:var(--mut);padding:32px 16px;border:1px dashed var(--bd);border-radius:14px}
</style>
</head>
<body>
<div class="wrap">
  <header>
    <h1 id="title">MakeCode Control</h1>
    <span id="conn" class="dot"><i></i><span>Live</span></span>
  </header>
  <div id="grid" class="grid"></div>
</div>
<script>
const SW=["#ff2424","#ffd9a0","#ffb300","#22c55e","#06b6d4","#3b82f6","#a855f7","#000000"];
const g=document.getElementById("grid"),tEl=document.getElementById("title"),cEl=document.getElementById("conn");
let sig="",busy=null,timers={};
const hex=n=>"#"+((n>>>0)&0xffffff).toString(16).padStart(6,"0");
const num=h=>parseInt(h.slice(1),16)&0xffffff;
function send(id,v,imm){
  if(imm){clearTimeout(timers[id]);delete timers[id];fetch("/ev?id="+id+"&v="+v).catch(()=>{});return;}
  if(timers[id])clearTimeout(timers[id]);
  timers[id]=setTimeout(()=>{delete timers[id];fetch("/ev?id="+id+"&v="+v).catch(()=>{});},75);
}
function render(d){
  tEl.textContent=d.title||"MakeCode Control";
  document.title=tEl.textContent;
  const ns=JSON.stringify(d.w.map(x=>[x.id,x.t,x.l,x.min,x.max]));
  if(ns===sig){sync(d.w);return;}
  sig=ns;g.innerHTML="";
  if(!d.w.length){g.innerHTML='<div class="empty">No controls added yet.</div>';return;}
  for(const w of d.w){
    const c=document.createElement("div");c.className="card";c.dataset.id=w.id;
    if(w.t==="c"){
      const h=hex(w.v||0);
      c.innerHTML='<div class="row"><span class="lbl"></span><span class="val" id="v'+w.id+'">'+h+'</span></div><div class="cwrap"><input type="color" id="i'+w.id+'" value="'+h+'"><div class="sws"></div></div>';
      c.querySelector(".lbl").textContent=w.l;
      const inp=c.querySelector("input"),val=c.querySelector(".val"),sws=c.querySelector(".sws");
      const setC=(hv,imm)=>{inp.value=hv;val.textContent=hv;send(w.id,num(hv),imm);};
      inp.onfocus=()=>busy=w.id;inp.onblur=()=>{if(busy===w.id)busy=null;};
      inp.oninput=()=>{busy=w.id;val.textContent=inp.value;send(w.id,num(inp.value),false);};
      inp.onchange=()=>{busy=null;setC(inp.value,true);};
      for(const sc of SW){const b=document.createElement("button");b.className="sw";b.style.background=sc;b.type="button";b.onclick=()=>setC(sc,true);sws.appendChild(b);}
    }else if(w.t==="b"){
      const b=document.createElement("button");b.className="btn";b.type="button";b.textContent=w.l;
      b.onclick=()=>send(w.id,1,true);c.appendChild(b);
    }else if(w.t==="s"){
      c.innerHTML='<div class="row"><span class="lbl"></span><span class="val" id="v'+w.id+'"></span></div><input type="range" id="i'+w.id+'" min="'+w.min+'" max="'+w.max+'" value="'+w.v+'">';
      c.querySelector(".lbl").textContent=w.l;
      const inp=c.querySelector("input"),val=c.querySelector(".val");val.textContent=w.v;
      inp.onpointerdown=()=>busy=w.id;inp.onpointerup=()=>{if(busy===w.id)busy=null;};
      inp.oninput=()=>{busy=w.id;val.textContent=inp.value;send(w.id,inp.value,false);};
      inp.onchange=()=>{busy=null;val.textContent=inp.value;send(w.id,inp.value,true);};
    }else if(w.t==="t"){
      c.innerHTML='<div class="st"><span class="lbl"></span><span class="val" id="v'+w.id+'"></span></div>';
      c.querySelector(".lbl").textContent=w.l;c.querySelector(".val").textContent=w.txt||"";
    }
    g.appendChild(c);
  }
}
function sync(ws){
  for(const w of ws){
    if(busy===w.id)continue;
    const vEl=document.getElementById("v"+w.id),iEl=document.getElementById("i"+w.id);
    if(w.t==="c"){const h=hex(w.v||0);if(vEl)vEl.textContent=h;if(iEl)iEl.value=h;}
    else if(w.t==="s"){if(vEl)vEl.textContent=w.v;if(iEl)iEl.value=w.v;}
    else if(w.t==="t"){if(vEl)vEl.textContent=w.txt||"";}
  }
}
async function poll(){
  try{
    const r=await fetch("/state",{cache:"no-store"});
    if(!r.ok)throw 0;
    const d=await r.json();
    cEl.className="dot";cEl.lastChild.textContent="Live";
    render(d);
  }catch{
    cEl.className="dot off";cEl.lastChild.textContent="Offline";
  }
}
poll();setInterval(poll,1500);
</script>
</body>
</html>)raw";

static void appendJsonEscaped(std::string& out, const char* s) {
    out.push_back('"');
    if (s) {
        for (; *s; ++s) {
            unsigned char c = (unsigned char)*s;
            if (c == '"') {
                out += "\\\"";
            } else if (c == '\\') {
                out += "\\\\";
            } else if (c == '\n') {
                out += "\\n";
            } else if (c == '\r') {
                out += "\\r";
            } else if (c == '\t') {
                out += "\\t";
            } else if (c >= 0x20) {
                out.push_back((char)c);
            }
        }
    }
    out.push_back('"');
}

esp_err_t dash_root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, DASHBOARD_HTML, sizeof(DASHBOARD_HTML) - 1);
    return ESP_OK;
}

esp_err_t dash_state_handler(httpd_req_t *req) {
    ensureDashMutex();
    std::string json;
    json.reserve(512);

    xSemaphoreTake(_dashMutex, portMAX_DELAY);
    json += "{\"title\":";
    appendJsonEscaped(json, _dashTitle);
    json += ",\"w\":[";
    bool first = true;
    for (int i = 0; i < _widgetCount; ++i) {
        const DashboardWidget& w = _widgets[i];
        if (!w.active)
            continue;
        if (!first)
            json.push_back(',');
        first = false;

        const char* t = "t";
        if (w.kind == WIDGET_COLOR)
            t = "c";
        else if (w.kind == WIDGET_BUTTON)
            t = "b";
        else if (w.kind == WIDGET_SLIDER)
            t = "s";

        char buf[64];
        snprintf(buf, sizeof(buf), "{\"id\":%d,\"t\":\"%s\",\"l\":", (int)w.id, t);
        json += buf;
        appendJsonEscaped(json, w.label);

        if (w.kind == WIDGET_COLOR) {
            snprintf(buf, sizeof(buf), ",\"v\":%d}", (int)(w.value & 0xffffff));
            json += buf;
        } else if (w.kind == WIDGET_SLIDER) {
            snprintf(buf, sizeof(buf), ",\"min\":%d,\"max\":%d,\"v\":%d}", (int)w.minVal, (int)w.maxVal, (int)w.value);
            json += buf;
        } else if (w.kind == WIDGET_STATUS) {
            json += ",\"txt\":";
            appendJsonEscaped(json, w.text);
            json.push_back('}');
        } else {
            json.push_back('}');
        }
    }
    json += "]}";
    xSemaphoreGive(_dashMutex);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

esp_err_t dash_ev_handler(httpd_req_t *req) {
    size_t bufLen = httpd_req_get_url_query_len(req) + 1;
    if (bufLen > 1 && bufLen < 128) {
        char buf[128];
        if (httpd_req_get_url_query_str(req, buf, bufLen) == ESP_OK) {
            char idStr[16];
            char valStr[24];
            if (httpd_query_key_value(buf, "id", idStr, sizeof(idStr)) == ESP_OK &&
                httpd_query_key_value(buf, "v", valStr, sizeof(valStr)) == ESP_OK) {
                int32_t id = atoi(idStr);
                int32_t val = atoi(valStr);
                bool raised = false;

                ensureDashMutex();
                xSemaphoreTake(_dashMutex, portMAX_DELAY);
                for (int i = 0; i < _widgetCount; ++i) {
                    DashboardWidget& w = _widgets[i];
                    if (w.active && w.id == id) {
                        if (w.kind == WIDGET_BUTTON) {
                            w.pendingClicks++;
                            raised = true;
                        } else if (w.kind == WIDGET_COLOR) {
                            w.value = val & 0xffffff;
                            w.pendingValue = true;
                            raised = true;
                        } else if (w.kind == WIDGET_SLIDER) {
                            if (val < w.minVal) val = w.minVal;
                            if (val > w.maxVal) val = w.maxVal;
                            w.value = val;
                            w.pendingValue = true;
                            raised = true;
                        }
                        break;
                    }
                }
                xSemaphoreGive(_dashMutex);

                if (raised) {
                    pxt::raiseEvent(_wifi::eventID(), (int)_wifi::WifiEvent::DashboardUpdated);
                }

                httpd_resp_set_status(req, "204 No Content");
                httpd_resp_set_hdr(req, "Cache-Control", "no-store");
                httpd_resp_send(req, "", 0);
                return ESP_OK;
            }
        }
    }
    httpd_resp_send_500(req);
    return ESP_OK;
}

httpd_uri_t dash_root_get = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = dash_root_handler,
    .user_ctx = NULL
};

httpd_uri_t dash_state_get = {
    .uri      = "/state",
    .method   = HTTP_GET,
    .handler  = dash_state_handler,
    .user_ctx = NULL
};

httpd_uri_t dash_ev_get = {
    .uri      = "/ev",
    .method   = HTTP_GET,
    .handler  = dash_ev_handler,
    .user_ctx = NULL
};

/** Start the embedded Web Dashboard server. */
//%
void dashboardStart(String title, bool captivePortal) {
    ensureDashMutex();
    xSemaphoreTake(_dashMutex, portMAX_DELAY);
    strlcpy(_dashTitle, title ? title->getUTF8Data() : "MakeCode Control", sizeof(_dashTitle));
    _captivePortalEnabled = captivePortal;
    xSemaphoreGive(_dashMutex);

    if (_serverMode == ServerMode::Login) {
        stopActiveServer();
    }

    if (_server == NULL) {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.lru_purge_enable = true;

        httpd_handle_t server = NULL;
        if (httpd_start(&server, &config) != ESP_OK) {
            LOG("failed to start dashboard HTTP server");
            return;
        }

        httpd_register_uri_handler(server, &dash_root_get);
        httpd_register_uri_handler(server, &dash_state_get);
        httpd_register_uri_handler(server, &dash_ev_get);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);

        _server = server;
        _serverMode = ServerMode::Dashboard;
        initMdnsOnce("makecode");
        LOG("dashboard HTTP server started");
    }

    if (captivePortal) {
        startCaptiveDns();
    } else {
        stopCaptiveDns();
    }
}

/** Stop the embedded Web Dashboard server and captive-portal DNS. */
//%
void dashboardStop() {
    if (_serverMode == ServerMode::Dashboard) {
        stopActiveServer();
    } else {
        stopCaptiveDns();
    }
}

/** Clear all registered Web Dashboard widgets. */
//%
void dashboardClear() {
    ensureDashMutex();
    xSemaphoreTake(_dashMutex, portMAX_DELAY);
    memset(_widgets, 0, sizeof(_widgets));
    _widgetCount = 0;
    xSemaphoreGive(_dashMutex);
}

/** Register or update a Web Dashboard widget. */
//%
void dashboardUpsertWidget(int id, int kind, String label, int minVal, int maxVal, int value, String text) {
    ensureDashMutex();
    xSemaphoreTake(_dashMutex, portMAX_DELAY);

    DashboardWidget* target = NULL;
    for (int i = 0; i < _widgetCount; ++i) {
        if (_widgets[i].active && _widgets[i].id == id) {
            target = &_widgets[i];
            break;
        }
    }
    if (!target && _widgetCount < MAX_WIDGETS) {
        target = &_widgets[_widgetCount++];
        memset(target, 0, sizeof(*target));
        target->active = true;
        target->id = id;
    }

    if (target) {
        target->kind = kind;
        strlcpy(target->label, label ? label->getUTF8Data() : "", sizeof(target->label));
        target->minVal = minVal;
        target->maxVal = maxVal;
        target->value = value;
        strlcpy(target->text, text ? text->getUTF8Data() : "", sizeof(target->text));
    }

    xSemaphoreGive(_dashMutex);
}

/** Pop the next queued (widgetId, value) event from the Web Dashboard as an 8-byte buffer, or empty buffer if none. */
//%
Buffer dashboardNextEvent() {
    ensureDashMutex();
    int32_t pair[2] = { 0, 0 };
    bool found = false;

    xSemaphoreTake(_dashMutex, portMAX_DELAY);
    for (int i = 0; i < _widgetCount; ++i) {
        DashboardWidget& w = _widgets[i];
        if (!w.active)
            continue;
        if (w.pendingClicks > 0) {
            w.pendingClicks--;
            pair[0] = w.id;
            pair[1] = 1;
            found = true;
            break;
        }
        if (w.pendingValue) {
            w.pendingValue = false;
            pair[0] = w.id;
            pair[1] = w.value;
            found = true;
            break;
        }
    }
    xSemaphoreGive(_dashMutex);

    if (!found)
        return mkBuffer(NULL, 0);
    return mkBuffer(pair, sizeof(pair));
}

}
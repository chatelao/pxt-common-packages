// Tests for wifi---esp32 (Wi-Fi controller, TCP/TLS mode, and XIAO RF antenna switch)
_wifi.setExternalAntenna(false);
let ctrl = net.instance().controller;
let aps = ctrl.scanNetworks();
let sock = ctrl.socket();
ctrl.socketClose(sock);
_wifi.setExternalAntenna(true);

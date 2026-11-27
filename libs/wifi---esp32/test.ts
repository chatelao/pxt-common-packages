// Tests for wifi---esp32 (Wi-Fi controller, TCP/TLS/UDP mode, IPv4 Buffer, and XIAO RF antenna switch)
_wifi.setExternalAntenna(false);
let ctrl = net.instance().controller;
let aps = ctrl.scanNetworks();
let axFlag = net.WifiAPFlags.IEEE_802_11AX;
let sock = ctrl.socket();
ctrl.socketClose(sock);
let udpSock = ctrl.socket();
ctrl.socketConnect(udpSock, Buffer.fromArray([255, 255, 255, 255]), 12345, net.UDP_MODE);
ctrl.socketWrite(udpSock, Buffer.fromUTF8("hello-udp"));
ctrl.socketClose(udpSock);
_wifi.setExternalAntenna(true);


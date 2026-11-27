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

net.onAccessPointEvent(net.AccessPointEvent.Started, () => { });
let apOk = net.startAccessPoint("MakeCode-Test", "12345678", 6, 4);
let apOn = net.isAccessPointEnabled();
let apIp = net.accessPointIP();
let apClients = net.accessPointClientCount();
net.stopAccessPoint();

web.onColorChanged("Tree Color", 0xff0066, (c: number) => {
    web.setStatus("Last Color", "" + c);
});
web.onButtonPressed("Next Mode", () => {
    web.setStatus("Mode", "Next");
});
web.onSliderChanged("Brightness", 0, 255, 128, (v: number) => {
    web.setSliderValue("Brightness", v);
});
web.startDashboard("Tree Control", "Tree-AP", "", true);
let curColor = web.color("Tree Color");
web.setColor("Tree Color", curColor);
let curBright = web.sliderValue("Brightness");
web.stopDashboard();

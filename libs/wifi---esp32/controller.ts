namespace net {
    //% shim=_wifi::_readLastAccessPointCredentials
    declare function _readLastAccessPointCredentials(): string[];

    const EV_ScanCompleted = 1000

    export class WifiController extends net.Controller {
        private networks: net.AccessPoint[]
        private inScan: boolean
        private _ssid: string

        constructor() {
            super()
            control.internalOnEvent(_wifi.eventID(), WifiEvent.ScanDone, () => this.scanDone())
            control.internalOnEvent(_wifi.eventID(), WifiEvent.Disconnected, () => {
                this.setConnected(false)
            })
            control.internalOnEvent(_wifi.eventID(), WifiEvent.GotIP, () => {
                this.setConnected(true)
            })
            control.internalOnEvent(_wifi.eventID(), WifiEvent.APStarted, () => {
                this.emitEvent(ControllerEvent.APStarted)
            })
            control.internalOnEvent(_wifi.eventID(), WifiEvent.APStopped, () => {
                this.emitEvent(ControllerEvent.APStopped)
            })
            control.internalOnEvent(_wifi.eventID(), WifiEvent.APStationConnected, () => {
                this.emitEvent(ControllerEvent.APStationConnected)
            })
            control.internalOnEvent(_wifi.eventID(), WifiEvent.APStationDisconnected, () => {
                this.emitEvent(ControllerEvent.APStationDisconnected)
            })
            control.internalOnEvent(_wifi.eventID(), WifiEvent.DashboardUpdated, () => {
                this.emitEvent(ControllerEvent.DashboardUpdated)
            })
        }

        private scanDone() {
            const buf = _wifi.scanResults()
            this.networks = []
            this.inScan = false
            if (!buf)
                return
            let i = 0
            const entrySize = 48
            while (i < buf.length) {
                const ap = net.AccessPoint.fromBuffer(buf.slice(i, entrySize))
                control.dmesg(`${ap.ssid} [${ap.rssi}dB]`)
                this.networks.push(ap)
                i += entrySize
            }
            control.raiseEvent(_wifi.eventID(), EV_ScanCompleted)
        }

        public scanNetworksCore(): net.AccessPoint[] {
            if (!this.inScan) {
                this.inScan = true
                _wifi.scanStart()
            }
            control.waitForEvent(_wifi.eventID(), EV_ScanCompleted)
            return this.networks
        }

        public startLoginServer(hostName: string): void {
            if (_wifi.isLoginServerEnabled())
                return
                
            this.disconnect()
            _wifi.startLoginServer(hostName);
            control.onEvent(_wifi.eventID(), WifiEvent.AccessPointCredentialsAvailable, () => {
                const credentials = _readLastAccessPointCredentials()
                if (!!credentials && credentials.length == 2) {
                    const ssid = credentials[0]
                    const pwd = credentials[1]
                    net.updateAccessPoint(ssid, pwd)
                    console.debug(`restarting...`);
                    pause(100);
                    control.reset();
                }
            })
            this.emitEvent(ControllerEvent.LoginServerStarted);
        }

        public isLoginServerEnabled(): boolean {
            return _wifi.isLoginServerEnabled()
        }

        public startAP(ssid: string, password: string = "", channel: number = 1, maxConnections: number = 4): boolean {
            const res = _wifi.startAP(ssid, password, channel, maxConnections)
            if (res != 0) {
                control.dmesg(`startAP [${ssid}] failed: ${res}`)
                return false
            }
            control.dmesg(`SoftAP [${ssid}] started`)
            return true
        }

        public stopAP(): void {
            _wifi.stopAP()
        }

        public isAPEnabled(): boolean {
            return _wifi.isAPEnabled()
        }

        get apIPaddress(): Buffer {
            return _wifi.isAPEnabled() ? _wifi.apIpInfo().slice(0, 4) : undefined
        }

        get apStationCount(): number {
            return _wifi.apStationCount()
        }

        public dashboardStart(title: string, captivePortal: boolean): void {
            _wifi.dashboardStart(title, captivePortal)
        }

        public dashboardStop(): void {
            _wifi.dashboardStop()
        }

        public dashboardClear(): void {
            _wifi.dashboardClear()
        }

        public dashboardUpsertWidget(id: number, kind: number, label: string, minVal: number, maxVal: number, value: number, text: string): void {
            _wifi.dashboardUpsertWidget(id, kind, label, minVal, maxVal, value, text)
        }

        public dashboardNextEvent(): Buffer {
            return _wifi.dashboardNextEvent()
        }

        public disconnectAP() {
            _wifi.disconnect()
        }

        public connectAP(ssid: string, pass: string) {
            control.dmesg(`connecting to [${ssid}]...`)
            this._ssid = ssid
            const res = _wifi.connect(ssid, pass)
            if (res != 0)
                return false
            pauseUntil(() => this.isConnected, 15000)
            control.dmesg(`${this.isConnected ? "" : "not "}connected to [${ssid}]`)
            return this.isConnected
        }

        public socket(): number {
            return _wifi.socketAlloc()
        }

        private logError(lbl: string, res: number) {
            if (res < 0) {
                control.dmesg(`sock ${lbl} failed: ${res}`)
                return false
            }
            return true
        }

        private formatDestinationHost(dest: string | Buffer): string {
            if (typeof dest === "string")
                return dest
            if (dest && dest.length === 4)
                return `${dest[0]}.${dest[1]}.${dest[2]}.${dest[3]}`
            throw "connection by IP not supported"
        }

        public socketConnect(socket_num: number, dest: string | Buffer, port: number, conn_mode = TCP_MODE): boolean {
            const host = this.formatDestinationHost(dest)
            if (conn_mode == TLS_MODE)
                return this.logError("connect", _wifi.socketConnectTLS(socket_num, host, port))
            if (conn_mode == UDP_MODE)
                return this.logError("connect", _wifi.socketConnectUDP(socket_num, host, port))
            return this.logError("connect", _wifi.socketConnectTCP(socket_num, host, port))
        }

        public socketWrite(socket_num: number, buffer: Buffer): void {
            this.logError("write", _wifi.socketWrite(socket_num, buffer))
        }

        public socketAvailable(socket_num: number): number {
            return _wifi.socketBytesAvailable(socket_num)
        }

        public socketRead(socket_num: number, size: number): Buffer {
            const r = _wifi.socketRead(socket_num, size)
            if (typeof r == "number") {
                this.logError("read", r)
                return undefined
            } else {
                return (r as any) as Buffer
            }
        }

        public socketClose(socket_num: number): void {
            this.logError("close", _wifi.socketClose(socket_num))
        }

        public hostbyName(hostname: string): Buffer {
            return undefined;
        }
        get isIdle(): boolean { return true; }
        get MACaddress(): Buffer { return control.deviceLongSerialNumber().slice(1, 6); }
        get IPaddress(): Buffer { return this.isConnected ? _wifi.ipInfo().slice(0, 4) : undefined; }
        get ssid(): string { return this.isConnected ? this._ssid : "" }
        get rssi(): number { return _wifi.rssi() }
        public ping(dest: string, ttl: number = 250): number { return -1; }

        public dataAvailableSrc(socket_num: number): number { return _wifi.eventID(); }
        public dataAvailableValue(socket_num: number): number { return 1000 + socket_num; }

    }

    // initialize Net.instance
    new net.Net(() => new WifiController())
}

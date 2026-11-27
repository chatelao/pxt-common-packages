/**
 * Networking, WiFi, web requests
 */
//% weight=1
//% advanced=true
//% icon="\uf1eb" color="#8446cf"
namespace net {
    /**
     * Default priority of net log messages
     **/
    export let logPriority: ConsolePriority = -1;
    export function log(msg: string) {
        console.add(logPriority, "net:" + msg);
    }
    export function debug(msg: string) {
        if (logPriority > ConsolePriority.Debug)
            console.add(ConsolePriority.Debug, "net:" + msg);
    }
    export function fail(reason: string) {
        net.log(`error: ${reason}`);
        throw reason;
    }

    export function monotonic(): number {
        return control.millis() / 1000.0;
    }

    export const enum WifiAPFlags {
        HasPassword = 0x1,
        WPS = 0x2,
        HasSecondaryChannelAbove = 0x4,
        HasSecondaryChannelBelow = 0x8,
        IEEE_802_11B = 0x100,
        IEEE_802_11A = 0x200,
        IEEE_802_11G = 0x400,
        IEEE_802_11N = 0x800,
        IEEE_802_11AC = 0x1000,
        IEEE_802_11AX = 0x2000,
        IEEE_802_LongRange = 0x8000,
    }

    export class AccessPoint {
        flags: WifiAPFlags
        rssi: number
        bssid: Buffer
        channel: number
        constructor(public ssid: string) { }
        static fromBuffer(buf: Buffer) {
            const name = buf.slice(16)
            let endp = name.length - 1
            while (endp > 0 && name[endp] == 0)
                endp--
            endp++
            const res = new AccessPoint(name.slice(0, endp).toString())
            const [flags, _reserved, rssi, channel] = buf.unpack("<IIbB")
            res.flags = flags
            res.rssi = rssi
            res.channel = channel
            res.bssid = buf.slice(10, 6)
            if (res.bssid.toArray(NumberFormat.UInt16LE).every(x => x == 0))
                res.bssid = null
            return res
        }
        toBuffer() {
            /*
            flags: APFlags
            reserved: u32
            rssi: i8 dB {typical_min = -100, typical_max = -20}
            channel: u8 {typical_min = 1, typical_max = 13}
            bssid: u8[6]
            ssid: string {max_bytes = 33}
            */
            const pref = Buffer.pack("<IIbB", [this.flags, 0, this.rssi, this.channel])
            const bssid = Buffer.create(6)
            if (this.bssid) bssid.write(0, this.bssid)
            return pref.concat(bssid).concat(Buffer.fromUTF8(this.ssid))
        }
    }

    export interface Socket {
        connect(): void;
        send(data: string | Buffer): void;
        read(contentLength: number): Buffer;
        close(): void;
        onOpen(handler: () => void): void;
        onClose(handler: () => void): void;
        onError(handler: (msg: string) => void): void;
        onMessage(handler: (data: Buffer) => void): void;
        setTimeout(millis: number): void;
        readLine(): string;
    }

    export class Net {
        private _controller: Controller;
        constructor(private factory: () => Controller) {
            Net.instance = this;
            this._controller = undefined; // null failed to initialize
        }

        static instance: Net;

        get controller(): net.Controller {
            if (this._controller === undefined) {
                net.log(`init controller`)
                this._controller = this.factory();
                if (!this._controller) {
                    net.log(`controller not found`)
                    this._controller = null;
                }
            }
            return this._controller;
        }

        /**
         * Scan for APs
         */
        scanNetworks(): net.AccessPoint[] {
            const c = this.controller;
            try {
                return c ? c.scanNetworks() : [];
            } catch (e) {
                console.error("" + e)
                return [];
            }
        }

        createSocket(host: string, port: number, secure: boolean): net.Socket {
            const c = this.controller;
            if (!c) return undefined;
            const socket = new net.ControllerSocket(c, host, port, secure ? net.TLS_MODE : net.TCP_MODE);
            return socket;
        }

        hostByName(host: string): string {
            const c = this.controller;
            if (!c) return undefined;
            const b = this.controller.hostbyName(host);
            if (b) return b.toString();
            return undefined;
        }
    }

    /**
     * Gets the current Net instance
     */
    export function instance(): Net {
        return net.Net.instance;
    }

    const AP_SECRETS_KEY = "wifi";
    const AP_PRI_KEY = "#wifipriority";
    /**
     * Gets the map of SSID -> password pairs
     */
    export function knownAccessPoints(): StringMap {
        return settings.deviceSecrets.readSecret(AP_SECRETS_KEY) || {};
    }

    export function clearAccessPoint(ssid: string) {
        const ap = knownAccessPoints()
        if (ap[ssid] !== undefined) {
            delete ap[ssid]
            settings.deviceSecrets.setSecret(AP_SECRETS_KEY, ap)
        }
    }

    export function updateAccessPoint(ssid: string, password: string) {
        const k: StringMap = {};
        k[ssid] = password;
        settings.deviceSecrets.updateSecret(AP_SECRETS_KEY, k);
    }

    export function setAccessPointPriority(ssid: string, pri: number) {
        const s = accessPointPriorities()
        if (s[ssid] != pri) {
            s[ssid] = pri
            settings.writeJSON(AP_PRI_KEY, s)
        }
    }

    export function accessPointPriorities() {
        return settings.readJSON(AP_PRI_KEY) || {}
    }

    export function clearAccessPoints() {
        settings.deviceSecrets.setSecret(AP_SECRETS_KEY, undefined);
    }

    export enum AccessPointEvent {
        //% block="started"
        Started = ControllerEvent.APStarted,
        //% block="stopped"
        Stopped = ControllerEvent.APStopped,
        //% block="client connected"
        ClientConnected = ControllerEvent.APStationConnected,
        //% block="client disconnected"
        ClientDisconnected = ControllerEvent.APStationDisconnected,
    }

    /**
     * Start a WiFi access point (SoftAP).
     * Leave password empty for an open network, or provide 8..63 characters for WPA2.
     */
    //% blockId=net_start_ap block="start access point $ssid||password $password channel $channel max clients $maxConnections"
    //% group="Access Point"
    //% ssid.defl="MakeCode-AP"
    //% password.defl=""
    //% channel.defl=1 channel.min=1 channel.max=13
    //% maxConnections.defl=4 maxConnections.min=1 maxConnections.max=10
    export function startAccessPoint(
        ssid: string,
        password: string = "",
        channel: number = 1,
        maxConnections: number = 4
    ): boolean {
        const inst = net.instance();
        const c = inst ? inst.controller : undefined;
        return c ? c.startAP(ssid, password, channel, maxConnections) : false;
    }

    /**
     * Stop the WiFi access point.
     */
    //% blockId=net_stop_ap block="stop access point"
    //% group="Access Point"
    export function stopAccessPoint(): void {
        const inst = net.instance();
        const c = inst ? inst.controller : undefined;
        if (c) c.stopAP();
    }

    /**
     * Returns true if the WiFi access point is currently active.
     */
    //% blockId=net_is_ap_enabled block="access point enabled"
    //% group="Access Point"
    export function isAccessPointEnabled(): boolean {
        const inst = net.instance();
        const c = inst ? inst.controller : undefined;
        return c ? c.isAPEnabled() : false;
    }

    /**
     * Returns the IPv4 address of the WiFi access point (typically "192.168.4.1"),
     * or an empty string if the access point is not enabled.
     */
    //% blockId=net_ap_ip block="access point IP"
    //% group="Access Point"
    export function accessPointIP(): string {
        const inst = net.instance();
        const c = inst ? inst.controller : undefined;
        if (!c || !c.isAPEnabled())
            return "";
        const ip = c.apIPaddress;
        if (!ip || ip.length < 4)
            return "";
        return `${ip[0]}.${ip[1]}.${ip[2]}.${ip[3]}`;
    }

    /**
     * Returns the number of clients currently connected to the WiFi access point.
     */
    //% blockId=net_ap_clients block="access point clients"
    //% group="Access Point"
    export function accessPointClientCount(): number {
        const inst = net.instance();
        const c = inst ? inst.controller : undefined;
        return c ? c.apStationCount : 0;
    }

    /**
     * Run code when an access point event occurs.
     */
    //% blockId=net_on_ap_event block="on access point $ev"
    //% group="Access Point"
    export function onAccessPointEvent(ev: AccessPointEvent, handler: () => void): void {
        const inst = net.instance();
        const c = inst ? inst.controller : undefined;
        if (c) c.onEvent(ev as number as ControllerEvent, handler);
    }
}
/**
 * Zigbee 3.0 (IEEE 802.15.4) Mesh Intercomm & Smart Home Automation
 */
//% color=#E67E22 weight=92 icon="\uf0e8" groups='["Network", "Intercomm", "Home Automation"]'
namespace zigbee {
    const EVT_JOINED = 1;
    const EVT_PACKET = 2;
    const EVT_SWITCH = 3;

    const PKT_TYPE_STRING = 1;
    const PKT_TYPE_VALUE = 2;

    let strHandler: (message: string) => void;
    let valHandler: (name: string, value: number) => void;
    let switchHandler: (on: boolean) => void;
    let rxRegistered = false;
    let switchRegistered = false;

    function ensureRxHandler(): void {
        if (rxRegistered)
            return;
        rxRegistered = true;
        control.onEvent(_zigbee.eventID(), EVT_PACKET, () => {
            let pkt = _zigbee.readPacket();
            while (pkt) {
                if (pkt.length >= 2) {
                    const kind = pkt[0];
                    if (kind === PKT_TYPE_STRING) {
                        const msgLen = pkt[1];
                        if (2 + msgLen <= pkt.length && strHandler) {
                            strHandler(pkt.slice(2, msgLen).toString());
                        }
                    } else if (kind === PKT_TYPE_VALUE) {
                        const nameLen = pkt[1];
                        if (2 + nameLen + 8 <= pkt.length && valHandler) {
                            const name = pkt.slice(2, nameLen).toString();
                            const value = pkt.getNumber(NumberFormat.Float64LE, 2 + nameLen);
                            valHandler(name, value);
                        }
                    }
                }
                pkt = _zigbee.readPacket();
            }
        });
    }

    /**
     * Start Zigbee 3.0 as Coordinator, Router, or End Device on the given 802.15.4 channel (11..26).
     */
    //% blockId=zigbee_start block="zigbee start as %role|on channel %channel"
    //% channel.min=11 channel.max=26 channel.defl=15
    //% group="Network" weight=100
    export function startNetwork(role: ZigbeeRole = ZigbeeRole.Router, channel: number = 15): boolean {
        return _zigbee.startNetwork(role, channel) === 0;
    }

    /**
     * Check whether the Zigbee device has formed or joined a network.
     */
    //% blockId=zigbee_is_joined block="zigbee is joined"
    //% group="Network" weight=95
    export function isJoined(): boolean {
        return _zigbee.isJoined();
    }

    /**
     * Run code when the Zigbee device forms or joins a network.
     */
    //% blockId=zigbee_on_joined block="on zigbee joined"
    //% group="Network" weight=90
    export function onJoined(handler: () => void): void {
        control.onEvent(_zigbee.eventID(), EVT_JOINED, handler);
    }

    /**
     * Broadcast a string message across the Zigbee mesh.
     */
    //% blockId=zigbee_send_string block="zigbee send string %message"
    //% group="Intercomm" weight=80
    export function sendString(message: string): void {
        const mBuf = Buffer.fromUTF8((message || "").slice(0, 180));
        const pkt = Buffer.create(2 + mBuf.length);
        pkt[0] = PKT_TYPE_STRING;
        pkt[1] = mBuf.length;
        pkt.write(2, mBuf);
        _zigbee.sendPacket(pkt);
    }

    /**
     * Broadcast a named numeric value across the Zigbee mesh.
     */
    //% blockId=zigbee_send_value block="zigbee send value %name = %value"
    //% group="Intercomm" weight=75
    export function sendValue(name: string, value: number): void {
        const nBuf = Buffer.fromUTF8((name || "").slice(0, 64));
        const pkt = Buffer.create(2 + nBuf.length + 8);
        pkt[0] = PKT_TYPE_VALUE;
        pkt[1] = nBuf.length;
        pkt.write(2, nBuf);
        pkt.setNumber(NumberFormat.Float64LE, 2 + nBuf.length, value);
        _zigbee.sendPacket(pkt);
    }

    /**
     * Run code when a string message arrives over Zigbee.
     */
    //% blockId=zigbee_on_string block="on zigbee received string"
    //% draggableParameters=reporter
    //% group="Intercomm" weight=70
    export function onReceivedString(handler: (message: string) => void): void {
        strHandler = handler;
        ensureRxHandler();
    }

    /**
     * Run code when a named numeric value arrives over Zigbee.
     */
    //% blockId=zigbee_on_value block="on zigbee received value"
    //% draggableParameters=reporter
    //% group="Intercomm" weight=65
    export function onReceivedValue(handler: (name: string, value: number) => void): void {
        valHandler = handler;
        ensureRxHandler();
    }

    /**
     * Set the Zigbee Home Automation On/Off switch state.
     */
    //% blockId=zigbee_set_switch block="zigbee set switch %on"
    //% group="Home Automation" weight=60
    export function setSwitch(on: boolean): void {
        _zigbee.setSwitch(on);
    }

    /**
     * Get the current Zigbee Home Automation On/Off switch state.
     */
    //% blockId=zigbee_get_switch block="zigbee switch state"
    //% group="Home Automation" weight=55
    export function getSwitch(): boolean {
        return _zigbee.getSwitch();
    }

    /**
     * Run code when a Zigbee hub or peer changes the On/Off switch state.
     */
    //% blockId=zigbee_on_switch block="on zigbee switch changed"
    //% draggableParameters=reporter
    //% group="Home Automation" weight=50
    export function onSwitchChanged(handler: (on: boolean) => void): void {
        switchHandler = handler;
        if (!switchRegistered) {
            switchRegistered = true;
            control.onEvent(_zigbee.eventID(), EVT_SWITCH, () => {
                if (switchHandler)
                    switchHandler(_zigbee.getSwitch());
            });
        }
    }

    /**
     * Report temperature in degrees Celsius to a Zigbee Home Automation hub.
     */
    //% blockId=zigbee_report_temp block="zigbee report temperature %celsius °C"
    //% group="Home Automation" weight=45
    export function reportTemperature(celsius: number): void {
        _zigbee.reportTemperature(Math.round(celsius * 100));
    }
}

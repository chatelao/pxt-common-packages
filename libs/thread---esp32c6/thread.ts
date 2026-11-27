/**
 * Thread (IEEE 802.15.4 / OpenThread) IPv6 Mesh Intercomm
 */
//% color=#00A88F weight=93 icon="\uf1e0" groups='["Network", "Send", "Receive"]'
namespace thread {
    const EVT_PACKET = 1;
    const EVT_ROLE_CHANGED = 2;

    const PKT_TYPE_STRING = 1;
    const PKT_TYPE_VALUE = 2;

    let strHandler: (topic: string, message: string) => void;
    let valHandler: (name: string, value: number) => void;
    let rxRegistered = false;

    function ensureRxHandler(): void {
        if (rxRegistered)
            return;
        rxRegistered = true;
        control.onEvent(_thread.eventID(), EVT_PACKET, () => {
            let pkt = _thread.readPacket();
            while (pkt) {
                if (pkt.length >= 2) {
                    const kind = pkt[0];
                    if (kind === PKT_TYPE_STRING) {
                        const topicLen = pkt[1];
                        if (2 + topicLen < pkt.length && strHandler) {
                            const topic = pkt.slice(2, topicLen).toString();
                            const msgLen = pkt[2 + topicLen];
                            const msg = pkt.slice(3 + topicLen, msgLen).toString();
                            strHandler(topic, msg);
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
                pkt = _thread.readPacket();
            }
        });
    }

    /**
     * Join or form a Thread mesh group on the specified 802.15.4 channel (11..26) and PAN ID.
     */
    //% blockId=thread_join_group block="thread join group|channel %channel|panId %panId|key %networkKey"
    //% channel.min=11 channel.max=26 channel.defl=15
    //% panId.defl=4660
    //% networkKey.defl="MakeCodeThread"
    //% group="Network" weight=100
    export function joinGroup(channel: number = 15, panId: number = 0x1234, networkKey: string = "MakeCodeThread"): boolean {
        return _thread.joinGroup(channel, panId, networkKey) === 0;
    }

    /**
     * Join an existing Thread network using an Active Operational Dataset TLV hex string.
     */
    //% group="Network" weight=95 advanced=true
    export function joinDatasetHex(datasetHex: string): boolean {
        return _thread.joinDatasetHex(datasetHex) === 0;
    }

    /**
     * Get the current OpenThread device role.
     */
    //% blockId=thread_role block="thread role"
    //% group="Network" weight=90
    export function role(): ThreadRole {
        return _thread.role() as ThreadRole;
    }

    /**
     * Register code to run when the Thread device role changes.
     */
    //% group="Network" weight=85 advanced=true
    export function onRoleChanged(handler: () => void): void {
        control.onEvent(_thread.eventID(), EVT_ROLE_CHANGED, handler);
    }

    /**
     * Send a topic and string message over the Thread IPv6 mesh.
     */
    //% blockId=thread_send_string block="thread send topic %topic message %message"
    //% group="Send" weight=80
    export function sendString(topic: string, message: string): void {
        const tBuf = Buffer.fromUTF8((topic || "").slice(0, 64));
        const mBuf = Buffer.fromUTF8((message || "").slice(0, 160));
        const pkt = Buffer.create(3 + tBuf.length + mBuf.length);
        pkt[0] = PKT_TYPE_STRING;
        pkt[1] = tBuf.length;
        pkt.write(2, tBuf);
        pkt[2 + tBuf.length] = mBuf.length;
        pkt.write(3 + tBuf.length, mBuf);
        _thread.sendPacket(pkt);
    }

    /**
     * Send a named numeric value over the Thread IPv6 mesh.
     */
    //% blockId=thread_send_value block="thread send value %name = %value"
    //% group="Send" weight=75
    export function sendValue(name: string, value: number): void {
        const nBuf = Buffer.fromUTF8((name || "").slice(0, 64));
        const pkt = Buffer.create(2 + nBuf.length + 8);
        pkt[0] = PKT_TYPE_VALUE;
        pkt[1] = nBuf.length;
        pkt.write(2, nBuf);
        pkt.setNumber(NumberFormat.Float64LE, 2 + nBuf.length, value);
        _thread.sendPacket(pkt);
    }

    /**
     * Run code when a topic/message string arrives over the Thread mesh.
     */
    //% blockId=thread_on_string block="on thread received string"
    //% draggableParameters=reporter
    //% group="Receive" weight=70
    export function onReceivedString(handler: (topic: string, message: string) => void): void {
        strHandler = handler;
        ensureRxHandler();
    }

    /**
     * Run code when a named numeric value arrives over the Thread mesh.
     */
    //% blockId=thread_on_value block="on thread received value"
    //% draggableParameters=reporter
    //% group="Receive" weight=65
    export function onReceivedValue(handler: (name: string, value: number) => void): void {
        valHandler = handler;
        ensureRxHandler();
    }
}

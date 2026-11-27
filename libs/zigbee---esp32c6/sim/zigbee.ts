namespace pxsim {
    export interface ZigbeeBoard extends EventBusBoard {
        zigbeeState?: ZigbeeState;
    }

    export class ZigbeeState {
        started = false;
        joined = false;
        role = 0;
        channel = 15;
        switchState = false;
        lastTemperature = 25;
        rxQueue: Uint8Array[] = [];
        private listenerRegistered = false;

        ensureListener() {
            if (this.listenerRegistered)
                return;
            this.listenerRegistered = true;
            board().addMessageListener((msg: SimulatorMessage) => {
                const z = msg as any;
                if (!z || z.type !== "zigbeepacket" || !this.started)
                    return;
                if (z.channel !== this.channel)
                    return;
                if (z.kind === "data" && z.data) {
                    this.rxQueue.push(new Uint8Array(z.data));
                    pxsim.control.raiseEvent(1237, 2, undefined);
                } else if (z.kind === "switch") {
                    this.switchState = !!z.on;
                    pxsim.control.raiseEvent(1237, 3, undefined);
                }
            });
        }
    }

    function getZigbeeState(): ZigbeeState {
        const b = board() as ZigbeeBoard;
        if (!b.zigbeeState)
            b.zigbeeState = new ZigbeeState();
        b.zigbeeState.ensureListener();
        return b.zigbeeState;
    }

    export namespace _zigbee {
        export function eventID(): number {
            return 1237;
        }

        export function startNetwork(role: number, channel: number): number {
            const st = getZigbeeState();
            st.role = role | 0;
            st.channel = Math.max(11, Math.min(26, channel | 0));
            st.started = true;
            st.joined = true;
            pxsim.control.raiseEvent(1237, 1, undefined);
            return 0;
        }

        export function sendPacket(data: RefBuffer): number {
            const st = getZigbeeState();
            if (!st.started || !data)
                return -1;
            Runtime.postMessage({
                type: "zigbeepacket",
                broadcast: true,
                kind: "data",
                channel: st.channel,
                data: data.data
            } as any);
            return data.data.length;
        }

        export function readPacket(): RefBuffer {
            const st = getZigbeeState();
            const item = st.rxQueue.shift();
            return item ? new RefBuffer(item) : undefined;
        }

        export function setSwitch(on: boolean): void {
            const st = getZigbeeState();
            st.switchState = !!on;
            Runtime.postMessage({
                type: "zigbeepacket",
                broadcast: true,
                kind: "switch",
                channel: st.channel,
                on: st.switchState
            } as any);
        }

        export function getSwitch(): boolean {
            return getZigbeeState().switchState;
        }

        export function reportTemperature(centiCelsius: number): void {
            getZigbeeState().lastTemperature = centiCelsius / 100;
        }

        export function isJoined(): boolean {
            return getZigbeeState().joined;
        }
    }
}

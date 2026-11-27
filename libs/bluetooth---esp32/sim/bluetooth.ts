namespace pxsim {
    export interface BleBoard extends EventBusBoard {
        bleState?: BleState;
    }

    export class BleState {
        uartStarted = false;
        connected = false;
        advertising = false;
        scanning = false;
        deviceName = "XIAO-C6";
        uartRxQueue: Uint8Array[] = [];
        advRxQueue: { rssi: number; data: Uint8Array }[] = [];
        private listenerRegistered = false;

        ensureListener() {
            if (this.listenerRegistered)
                return;
            this.listenerRegistered = true;
            board().addMessageListener((msg: SimulatorMessage) => {
                const b = msg as any;
                if (!b || b.type !== "blepacket")
                    return;
                if (b.kind === "adv" && this.scanning && b.data) {
                    this.advRxQueue.push({ rssi: b.rssi ?? -55, data: new Uint8Array(b.data) });
                    pxsim.control.raiseEvent(1235, 4, undefined);
                } else if (b.kind === "uart" && this.uartStarted && b.data) {
                    this.uartRxQueue.push(new Uint8Array(b.data));
                    pxsim.control.raiseEvent(1235, 3, undefined);
                } else if (b.kind === "connect" && this.uartStarted) {
                    this.connected = true;
                    pxsim.control.raiseEvent(1235, 1, undefined);
                } else if (b.kind === "disconnect" && this.uartStarted) {
                    this.connected = false;
                    pxsim.control.raiseEvent(1235, 2, undefined);
                }
            });
        }
    }

    function getBleState(): BleState {
        const b = board() as BleBoard;
        if (!b.bleState)
            b.bleState = new BleState();
        b.bleState.ensureListener();
        return b.bleState;
    }

    export namespace _ble {
        export function eventID(): number {
            return 1235;
        }

        export function startUartService(): void {
            const st = getBleState();
            st.uartStarted = true;
            st.advertising = true;
        }

        export function uartWriteBuffer(data: RefBuffer): number {
            const st = getBleState();
            if (!st.connected || !data)
                return -1;
            Runtime.postMessage({
                type: "blepacket",
                broadcast: true,
                kind: "uart",
                data: data.data
            } as any);
            return data.data.length;
        }

        export function uartReadBuffer(): RefBuffer {
            const st = getBleState();
            const item = st.uartRxQueue.shift();
            return item ? new RefBuffer(item) : undefined;
        }

        export function isConnected(): boolean {
            return getBleState().connected;
        }

        export function setDeviceName(name: string): void {
            getBleState().deviceName = name || "XIAO-C6";
        }

        export function advertiseData(data: RefBuffer, intervalMs: number): void {
            const st = getBleState();
            st.advertising = true;
            if (!data)
                return;
            const payload = data.data.slice(0, 24);
            Runtime.postMessage({
                type: "blepacket",
                broadcast: true,
                kind: "adv",
                rssi: -52,
                intervalMs,
                data: payload
            } as any);
        }

        export function stopAdvertising(): void {
            getBleState().advertising = false;
        }

        export function startScanning(): void {
            getBleState().scanning = true;
        }

        export function stopScanning(): void {
            getBleState().scanning = false;
        }

        export function readAdvertisement(): RefBuffer {
            const st = getBleState();
            const item = st.advRxQueue.shift();
            if (!item)
                return undefined;
            const len = Math.min(item.data.length, 31);
            const buf = new Uint8Array(8 + len);
            buf[0] = item.rssi & 0xff;
            buf[7] = len;
            buf.set(item.data.slice(0, len), 8);
            return new RefBuffer(buf);
        }
    }
}

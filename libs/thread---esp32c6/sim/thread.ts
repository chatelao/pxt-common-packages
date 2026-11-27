namespace pxsim {
    export interface ThreadBoard extends EventBusBoard {
        threadState?: ThreadState;
    }

    export class ThreadState {
        joined = false;
        channel = 15;
        panId = 0x1234;
        networkKey = "MakeCodeThread";
        currentRole = 0;
        rxQueue: Uint8Array[] = [];
        private listenerRegistered = false;

        ensureListener() {
            if (this.listenerRegistered)
                return;
            this.listenerRegistered = true;
            board().addMessageListener((msg: SimulatorMessage) => {
                const t = msg as any;
                if (!t || t.type !== "threadpacket" || !this.joined)
                    return;
                if (t.channel === this.channel && t.panId === this.panId &&
                    t.networkKey === this.networkKey && t.data) {
                    this.rxQueue.push(new Uint8Array(t.data));
                    pxsim.control.raiseEvent(1236, 1, undefined);
                }
            });
        }
    }

    function getThreadState(): ThreadState {
        const b = board() as ThreadBoard;
        if (!b.threadState)
            b.threadState = new ThreadState();
        b.threadState.ensureListener();
        return b.threadState;
    }

    export namespace _thread {
        export function eventID(): number {
            return 1236;
        }

        export function joinGroup(channel: number, panId: number, networkKey: string): number {
            const st = getThreadState();
            st.channel = Math.max(11, Math.min(26, channel | 0));
            st.panId = panId & 0xffff;
            st.networkKey = networkKey || "MakeCodeThread";
            st.joined = true;
            st.currentRole = 3; // Router
            pxsim.control.raiseEvent(1236, 2, undefined);
            return 0;
        }

        export function joinDatasetHex(datasetHex: string): number {
            if (!datasetHex || (datasetHex.length & 1))
                return -2;
            const st = getThreadState();
            st.joined = true;
            st.networkKey = datasetHex;
            st.currentRole = 2; // Child
            pxsim.control.raiseEvent(1236, 2, undefined);
            return 0;
        }

        export function sendPacket(data: RefBuffer): number {
            const st = getThreadState();
            if (!st.joined || !data)
                return -1;
            Runtime.postMessage({
                type: "threadpacket",
                broadcast: true,
                channel: st.channel,
                panId: st.panId,
                networkKey: st.networkKey,
                data: data.data
            } as any);
            return data.data.length;
        }

        export function readPacket(): RefBuffer {
            const st = getThreadState();
            const item = st.rxQueue.shift();
            return item ? new RefBuffer(item) : undefined;
        }

        export function role(): number {
            return getThreadState().currentRole;
        }
    }
}

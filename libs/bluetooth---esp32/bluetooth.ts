/**
 * Bluetooth Low Energy (BLE 5.3) UART, Beacons, and Scanning
 */
//% color=#0082FB weight=95 icon="\uf294" groups='["UART", "Beacons", "Scanner", "Connection"]'
namespace bluetooth {
    const EVT_CONNECTED = 1;
    const EVT_DISCONNECTED = 2;
    const EVT_UART_RX = 3;
    const EVT_ADV_RECEIVED = 4;

    let uartHandler: () => void;
    let advHandler: (data: Buffer, rssi: number) => void;
    let uartListenerRegistered = false;
    let advListenerRegistered = false;

    /**
     * Start the Bluetooth Nordic UART Service (NUS) for serial-over-BLE communication.
     */
    //% blockId=ble_start_uart block="bluetooth start uart service"
    //% group="UART" weight=100
    export function startUartService(): void {
        _ble.startUartService();
    }

    /**
     * Send a string over Bluetooth UART.
     */
    //% blockId=ble_uart_write_string block="bluetooth uart write string %data"
    //% group="UART" weight=95
    export function uartWriteString(data: string): void {
        if (data)
            _ble.uartWriteBuffer(Buffer.fromUTF8(data));
    }

    /**
     * Send a buffer over Bluetooth UART.
     */
    //% group="UART" weight=90
    export function uartWriteBuffer(data: Buffer): void {
        if (data)
            _ble.uartWriteBuffer(data);
    }

    /**
     * Read the next pending string from Bluetooth UART, or empty string if none.
     */
    //% blockId=ble_uart_read_string block="bluetooth uart read string"
    //% group="UART" weight=85
    export function uartReadString(): string {
        const buf = _ble.uartReadBuffer();
        return buf ? buf.toString() : "";
    }

    /**
     * Read the next pending buffer from Bluetooth UART, or null if none.
     */
    //% group="UART" weight=80
    export function uartReadBuffer(): Buffer {
        return _ble.uartReadBuffer();
    }

    /**
     * Run code when data is received over Bluetooth UART.
     */
    //% blockId=ble_on_uart_rx block="on bluetooth uart data received"
    //% group="UART" weight=75
    export function onUartDataReceived(handler: () => void): void {
        uartHandler = handler;
        if (!uartListenerRegistered) {
            uartListenerRegistered = true;
            control.onEvent(_ble.eventID(), EVT_UART_RX, () => {
                if (uartHandler)
                    uartHandler();
            });
        }
    }

    /**
     * Run code when a Bluetooth central connects.
     */
    //% blockId=ble_on_connected block="on bluetooth connected"
    //% group="Connection" weight=70
    export function onConnected(handler: () => void): void {
        control.onEvent(_ble.eventID(), EVT_CONNECTED, handler);
    }

    /**
     * Run code when a Bluetooth central disconnects.
     */
    //% blockId=ble_on_disconnected block="on bluetooth disconnected"
    //% group="Connection" weight=65
    export function onDisconnected(handler: () => void): void {
        control.onEvent(_ble.eventID(), EVT_DISCONNECTED, handler);
    }

    /**
     * Check whether a Bluetooth device is currently connected.
     */
    //% blockId=ble_is_connected block="bluetooth is connected"
    //% group="Connection" weight=60
    export function isConnected(): boolean {
        return _ble.isConnected();
    }

    /**
     * Set the Bluetooth GAP device name.
     */
    //% blockId=ble_set_name block="bluetooth set device name %name"
    //% group="Connection" weight=55
    export function setDeviceName(name: string): void {
        _ble.setDeviceName(name);
    }

    /**
     * Broadcast a non-connectable BLE beacon buffer (up to 24 bytes).
     */
    //% group="Beacons" weight=50
    export function advertiseData(data: Buffer, intervalMs: number = 100): void {
        if (data)
            _ble.advertiseData(data, intervalMs);
    }

    /**
     * Broadcast a non-connectable BLE beacon text message (up to 24 bytes).
     */
    //% blockId=ble_advertise_string block="bluetooth advertise text %text"
    //% group="Beacons" weight=48
    export function advertiseString(text: string, intervalMs: number = 100): void {
        if (text)
            _ble.advertiseData(Buffer.fromUTF8(text), intervalMs);
    }

    /**
     * Stop broadcasting BLE advertisements.
     */
    //% blockId=ble_stop_adv block="bluetooth stop advertising"
    //% group="Beacons" weight=45
    export function stopAdvertising(): void {
        _ble.stopAdvertising();
    }

    /**
     * Start scanning for nearby BLE advertisements.
     */
    //% blockId=ble_start_scan block="bluetooth start scanning"
    //% group="Scanner" weight=40
    export function startScanning(): void {
        _ble.startScanning();
    }

    /**
     * Stop scanning for BLE advertisements.
     */
    //% blockId=ble_stop_scan block="bluetooth stop scanning"
    //% group="Scanner" weight=35
    export function stopScanning(): void {
        _ble.stopScanning();
    }

    /**
     * Run code when a BLE advertisement packet is received.
     */
    //% blockId=ble_on_adv block="on bluetooth advertisement received"
    //% draggableParameters=reporter
    //% group="Scanner" weight=30
    export function onAdvertisementReceived(handler: (data: Buffer, rssi: number) => void): void {
        advHandler = handler;
        if (!advListenerRegistered) {
            advListenerRegistered = true;
            control.onEvent(_ble.eventID(), EVT_ADV_RECEIVED, () => {
                let pkt = _ble.readAdvertisement();
                while (pkt) {
                    if (pkt.length >= 8 && advHandler) {
                        const rssi = pkt.getNumber(NumberFormat.Int8LE, 0);
                        const len = pkt[7];
                        const payload = pkt.slice(8, len);
                        advHandler(payload, rssi);
                    }
                    pkt = _ble.readAdvertisement();
                }
            });
        }
    }
}

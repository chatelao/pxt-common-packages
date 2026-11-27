// Auto-generated. Do not edit.
declare namespace _ble {

    /** Initialize BLE stack and start Nordic UART Service advertising. */
    //% shim=_ble::startUartService
    function startUartService(): void;

    /** Write a buffer over Nordic UART Service TX notifications. */
    //% shim=_ble::uartWriteBuffer
    function uartWriteBuffer(data: Buffer): int32;

    /** Read the next received Nordic UART Service RX packet, or null if empty. */
    //% shim=_ble::uartReadBuffer
    function uartReadBuffer(): Buffer;

    /** Check whether a BLE central is currently connected. */
    //% shim=_ble::isConnected
    function isConnected(): boolean;

    /** Set the BLE GAP device name. */
    //% shim=_ble::setDeviceName
    function setDeviceName(name: string): void;

    /** Broadcast non-connectable BLE manufacturer data (up to 24 bytes). */
    //% shim=_ble::advertiseData
    function advertiseData(data: Buffer, intervalMs: int32): void;

    /** Stop BLE advertising. */
    //% shim=_ble::stopAdvertising
    function stopAdvertising(): void;

    /** Start scanning for BLE advertisements. */
    //% shim=_ble::startScanning
    function startScanning(): void;

    /** Stop scanning for BLE advertisements. */
    //% shim=_ble::stopScanning
    function stopScanning(): void;

    /** Read the next queued BLE advertisement packet: [rssi:i8, addr:u8[6], len:u8, data:u8[len]]. */
    //% shim=_ble::readAdvertisement
    function readAdvertisement(): Buffer;

    /** Get the event source ID used for BLE events. */
    //% shim=_ble::eventID
    function eventID(): int32;
}

// Auto-generated. Do not edit. Really.

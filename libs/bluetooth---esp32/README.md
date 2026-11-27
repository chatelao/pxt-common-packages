# Bluetooth Low Energy (ESP32 / ESP32-C6)

Provides Bluetooth 5.3 LE support via Apache NimBLE:
- Nordic UART Service (NUS) serial bridge (`startUartService`, `uartWriteString`, `onUartDataReceived`)
- Connectionless BLE advertising & passive scanning (`advertiseData`, `advertiseString`, `startScanning`, `onAdvertisementReceived`)

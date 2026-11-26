// Tests for serial---esp32 (USB-Serial/JTAG console + UART1 redirect)
serial.writeLine("XIAO ESP32-C6 serial ready");
serial.writeValue("adc", 512);
serial.onDelimiterReceived(Delimiters.NewLine, () => {
    let line = serial.readUntil(Delimiters.NewLine);
    serial.writeLine("echo: " + line);
});

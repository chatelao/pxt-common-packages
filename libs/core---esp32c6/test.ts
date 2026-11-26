// Tests for core---esp32c6 peripherals (GPIO, ADC, PWM, Servo, I2C, SPI)
pins.pinByCfg(DAL.CFG_PIN_D0).digitalWrite(true);
let d0 = pins.pinByCfg(DAL.CFG_PIN_D0).digitalRead();
let a0 = (pins.pinByCfg(DAL.CFG_PIN_A0) as AnalogInPin).analogRead();
(pins.pinByCfg(DAL.CFG_PIN_D2) as AnalogOutPin).analogWrite(512);
(pins.pinByCfg(DAL.CFG_PIN_D3) as PwmOnlyPin).servoWrite(90);
let i2cBuf = pins.i2cReadBuffer(0x3c, 2);
pins.i2cWriteNumber(0x3c, 0x00, NumberFormat.UInt8BE);
pins.spiFrequency(1000000);
pins.spiMode(0);
let spiVal = pins.spiWrite(0x55);
control.dmesg(`c6 test: ${d0} ${a0} ${spiVal} ${i2cBuf ? i2cBuf.length : 0}`);

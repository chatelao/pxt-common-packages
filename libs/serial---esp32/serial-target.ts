namespace serial {
    export function createSerial(tx: DigitalInOutPin, rx: DigitalInOutPin, id?: number): Serial {
        const dev = serial.internalCreateSerialDevice(tx, rx, id || 0);
        return new Serial(dev);
    }

    let _device: Serial;
    export function device(): Serial {
        if (!_device) {
            _device = new Serial(serial.internalCreateSerialDevice(null, null, 3000));
        }
        return _device;
    }

    export function redirect(tx: DigitalInOutPin, rx: DigitalInOutPin, rate: BaudRate) {
        const ser = device();
        if (ser)
            ser.serialDevice.redirect(tx, rx, rate);
    }
}

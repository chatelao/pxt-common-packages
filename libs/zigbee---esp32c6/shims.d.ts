// Auto-generated. Do not edit.
declare namespace _zigbee {

    /** Start Zigbee 3.0 stack with role (0=Coordinator, 1=Router, 2=EndDevice) on channel (11..26). */
    //% shim=_zigbee::startNetwork
    function startNetwork(role: int32, channel: int32): int32;

    /** Send a Zigbee intercomm packet over the custom MakeCode cluster (0xFC00). */
    //% shim=_zigbee::sendPacket
    function sendPacket(data: Buffer): int32;

    /** Read the next queued Zigbee intercomm packet, or null if empty. */
    //% shim=_zigbee::readPacket
    function readPacket(): Buffer;

    /** Set the local ZCL On/Off cluster attribute and report to bound peers. */
    //% shim=_zigbee::setSwitch
    function setSwitch(on: boolean): void;

    /** Get the current ZCL On/Off switch state. */
    //% shim=_zigbee::getSwitch
    function getSwitch(): boolean;

    /** Report temperature (in 0.01 degrees Celsius) on the ZCL Temperature Measurement cluster (0x0402). */
    //% shim=_zigbee::reportTemperature
    function reportTemperature(centiCelsius: int32): void;

    /** Check whether the device has joined/formed a Zigbee network. */
    //% shim=_zigbee::isJoined
    function isJoined(): boolean;

    /** Get event source ID for Zigbee events. */
    //% shim=_zigbee::eventID
    function eventID(): int32;
}

// Auto-generated. Do not edit. Really.

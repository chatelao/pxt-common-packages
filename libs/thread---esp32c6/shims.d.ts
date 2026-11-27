// Auto-generated. Do not edit.
declare namespace _thread {

    /** Join or form a Thread mesh group on the given 802.15.4 channel (11..26) and PAN ID. */
    //% shim=_thread::joinGroup
    function joinGroup(channel: int32, panId: int32, networkKey: string): int32;

    /** Join an existing Thread network using an Active Operational Dataset TLV hex string. */
    //% shim=_thread::joinDatasetHex
    function joinDatasetHex(datasetHex: string): int32;

    /** Send a raw Thread mesh intercomm packet over IPv6 multicast (ff03::1). */
    //% shim=_thread::sendPacket
    function sendPacket(data: Buffer): int32;

    /** Read the next queued Thread mesh intercomm packet, or null if empty. */
    //% shim=_thread::readPacket
    function readPacket(): Buffer;

    /** Get current OpenThread device role (0=Disabled..4=Leader). */
    //% shim=_thread::role
    function role(): int32;

    /** Get event source ID for Thread events. */
    //% shim=_thread::eventID
    function eventID(): int32;
}

// Auto-generated. Do not edit. Really.

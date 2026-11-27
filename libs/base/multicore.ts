namespace loops {
    /**
     * Runs code once on an additional processor core when the program starts.
     * @param core the processor core index (1 or higher)
     * @param a code to execute
     */
    //% help=loops/on-start-on-core weight=99 afterOnStart=true
    //% blockId=loops_on_start_core block="on start on $core"
    //% core.fieldEditor="coreselector" core.defl=1 core.fieldOptions.decompileLiterals=1
    //% multiCore=1
    //% shim=pxt::runOnStartOnCore
    export declare function onStartOnCore(core: number, a: () => void): void;

    /**
     * Repeats the code forever on an additional processor core.
     * @param core the processor core index (1 or higher)
     * @param a code to execute
     */
    //% help=loops/forever-on-core weight=98 afterOnStart=true
    //% blockId=loops_forever_core block="forever on $core"
    //% core.fieldEditor="coreselector" core.defl=1 core.fieldOptions.decompileLiterals=1
    //% multiCore=1
    //% shim=pxt::runForeverOnCore
    export declare function foreverOnCore(core: number, a: () => void): void;
}

namespace control {
    /**
     * Returns the index of the processor core currently executing this code (0 = primary core).
     */
    //% help=control/core-id weight=8
    //% shim=pxt::coreId
    export declare function coreId(): number;

    /**
     * Returns the number of available processor cores on this device.
     */
    //% help=control/core-count weight=7
    //% shim=pxt::coreCount
    export declare function coreCount(): number;
}

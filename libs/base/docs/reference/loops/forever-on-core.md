# forever on core

Repeat the code inside this block forever on an additional processor core (such as Core 1 on a dual-core microcontroller).

```sig
loops.foreverOnCore(1, () => {
})
```

On multi-core boards (such as Raspberry Pi Pico RP2040 or dual-core ESP32), `forever on core` runs independently in parallel with `on start` and `forever` on Core 0.

## Parameters

* **core**: the processor core index (`1` or higher) to run this loop on.
* **body**: the code to repeat forever on the selected core.

## See also

[forever](/reference/loops/forever), [on start on core](/reference/loops/on-start-on-core)

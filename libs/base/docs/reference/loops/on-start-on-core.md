# on start on core

Run setup code once on an additional processor core when the program starts, before any `forever on core` loop on that core begins.

```sig
loops.onStartOnCore(1, () => {
})
```

## Parameters

* **core**: the processor core index (`1` or higher) to run this setup code on.
* **body**: the code to run once on the selected core.

## See also

[forever on core](/reference/loops/forever-on-core), [forever](/reference/loops/forever)

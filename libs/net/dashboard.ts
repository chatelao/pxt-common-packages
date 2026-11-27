/**
 * Drag-and-drop Web Control Dashboard over WiFi Access Point
 */
//% color="#00897B" icon="\uf1de" weight=45
//% groups='["Setup", "Controls", "Values"]'
namespace web {
    const enum WidgetKind {
        Color = 1,
        Button = 2,
        Slider = 3,
        Status = 4,
    }

    interface WidgetEntry {
        id: number;
        kind: WidgetKind;
        label: string;
        minVal: number;
        maxVal: number;
        value: number;
        text: string;
        onNumber?: (v: number) => void;
        onClick?: () => void;
    }

    let _widgets: WidgetEntry[] = [];
    let _nextId = 0;
    let _listenerInstalled = false;

    function getController(): net.Controller {
        const inst = net.instance();
        return inst ? inst.controller : undefined;
    }

    function ensureListener(): void {
        if (_listenerInstalled)
            return;
        const c = getController();
        if (!c)
            return;
        _listenerInstalled = true;
        c.onEvent(net.ControllerEvent.DashboardUpdated, () => {
            while (true) {
                const ev = c.dashboardNextEvent();
                if (!ev || ev.length < 8)
                    break;
                const [id, val] = ev.unpack("<ii");
                const w = findById(id);
                if (!w)
                    continue;
                if (w.kind == WidgetKind.Button) {
                    if (w.onClick)
                        w.onClick();
                } else {
                    w.value = val;
                    if (w.onNumber)
                        w.onNumber(val);
                }
            }
        });
    }

    function findById(id: number): WidgetEntry {
        for (let i = 0; i < _widgets.length; i++) {
            if (_widgets[i].id === id)
                return _widgets[i];
        }
        return undefined;
    }

    function findOrCreate(kind: WidgetKind, label: string): WidgetEntry {
        for (let i = 0; i < _widgets.length; i++) {
            if (_widgets[i].label === label && _widgets[i].kind === kind)
                return _widgets[i];
        }
        const entry: WidgetEntry = {
            id: _nextId++,
            kind,
            label,
            minVal: 0,
            maxVal: 255,
            value: 0,
            text: "",
        };
        _widgets.push(entry);
        return entry;
    }

    function syncWidget(w: WidgetEntry): void {
        const c = getController();
        if (c)
            c.dashboardUpsertWidget(w.id, w.kind, w.label, w.minVal, w.maxVal, w.value, w.text);
    }

    /**
     * Start the WiFi Access Point (if not already active) and host the interactive Web Dashboard.
     * When a phone or laptop joins the WiFi network, the dashboard pops up automatically.
     */
    //% blockId=web_start_dashboard
    //% block="start web dashboard $title||ssid $ssid password $password captive portal $captivePortal"
    //% title.defl="MakeCode Control" ssid.defl="MakeCode-AP" password.defl="" captivePortal.defl=true
    //% group="Setup" weight=100
    export function startDashboard(
        title: string = "MakeCode Control",
        ssid: string = "MakeCode-AP",
        password: string = "",
        captivePortal: boolean = true
    ): boolean {
        const c = getController();
        if (!c)
            return false;
        ensureListener();
        if (!c.isAPEnabled()) {
            if (!c.startAP(ssid, password, 1, 4))
                return false;
        }
        for (let i = 0; i < _widgets.length; i++)
            syncWidget(_widgets[i]);
        c.dashboardStart(title, captivePortal);
        return true;
    }

    /**
     * Stop the Web Dashboard HTTP and captive-portal DNS servers.
     */
    //% blockId=web_stop_dashboard block="stop web dashboard"
    //% group="Setup" weight=90
    export function stopDashboard(): void {
        const c = getController();
        if (c)
            c.dashboardStop();
    }

    /**
     * Add a Color Picker to the webpage and run code when the user picks a color.
     * `color` is a 24-bit RGB number (0xRRGGBB), compatible with NeoPixel blocks.
     */
    //% blockId=web_on_color_changed
    //% block="on web color $label default $defaultColor=colorNumberPicker changed"
    //% draggableParameters="reporter"
    //% label.defl="Color" defaultColor.defl=0xff0066
    //% group="Controls" weight=90
    export function onColorChanged(
        label: string,
        defaultColor: number,
        handler: (color: number) => void
    ): void {
        ensureListener();
        const w = findOrCreate(WidgetKind.Color, label);
        w.value = defaultColor & 0xffffff;
        w.onNumber = handler;
        syncWidget(w);
    }

    /**
     * Add a Button to the webpage and run code when it is pressed.
     */
    //% blockId=web_on_button_pressed
    //% block="on web button $label pressed"
    //% label.defl="Action"
    //% group="Controls" weight=85
    export function onButtonPressed(
        label: string,
        handler: () => void
    ): void {
        ensureListener();
        const w = findOrCreate(WidgetKind.Button, label);
        w.onClick = handler;
        syncWidget(w);
    }

    /**
     * Add a Slider to the webpage and run code when its value changes.
     */
    //% blockId=web_on_slider_changed
    //% block="on web slider $label min $min max $max default $defaultValue changed"
    //% draggableParameters="reporter"
    //% label.defl="Brightness" min.defl=0 max.defl=255 defaultValue.defl=128
    //% group="Controls" weight=80
    export function onSliderChanged(
        label: string,
        min: number,
        max: number,
        defaultValue: number,
        handler: (value: number) => void
    ): void {
        ensureListener();
        const lo = Math.min(min, max) | 0;
        const hi = Math.max(min, max) | 0;
        const clamped = Math.max(lo, Math.min(hi, defaultValue | 0));
        const w = findOrCreate(WidgetKind.Slider, label);
        w.minVal = lo;
        w.maxVal = hi;
        w.value = clamped;
        w.onNumber = handler;
        syncWidget(w);
    }

    /**
     * Get the current 24-bit color (0xRRGGBB) of a web color picker.
     */
    //% blockId=web_get_color block="web color $label"
    //% label.defl="Color"
    //% group="Values" weight=70
    export function color(label: string): number {
        const w = findOrCreate(WidgetKind.Color, label);
        return w.value & 0xffffff;
    }

    /**
     * Update a web color picker's displayed color from the board.
     */
    //% blockId=web_set_color block="set web color $label to $rgb=colorNumberPicker"
    //% label.defl="Color" rgb.defl=0xff0066
    //% group="Values" weight=65
    export function setColor(label: string, rgb: number): void {
        const w = findOrCreate(WidgetKind.Color, label);
        w.value = rgb & 0xffffff;
        syncWidget(w);
    }

    /**
     * Get the current numeric value of a web slider.
     */
    //% blockId=web_get_slider block="web slider $label"
    //% label.defl="Brightness"
    //% group="Values" weight=60
    export function sliderValue(label: string): number {
        const w = findOrCreate(WidgetKind.Slider, label);
        return w.value;
    }

    /**
     * Update a web slider's displayed value from the board.
     */
    //% blockId=web_set_slider block="set web slider $label to $value"
    //% label.defl="Brightness" value.defl=128
    //% group="Values" weight=55
    export function setSliderValue(label: string, value: number): void {
        const w = findOrCreate(WidgetKind.Slider, label);
        const v = Math.max(w.minVal, Math.min(w.maxVal, value | 0));
        w.value = v;
        syncWidget(w);
    }

    /**
     * Display or update a live text readout on the webpage.
     */
    //% blockId=web_set_status block="set web status $label to $text"
    //% label.defl="Status" text.defl="Ready"
    //% group="Values" weight=50
    export function setStatus(label: string, text: string): void {
        const w = findOrCreate(WidgetKind.Status, label);
        w.text = text || "";
        syncWidget(w);
    }
}

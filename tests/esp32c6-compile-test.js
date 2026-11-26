const fs = require("fs");
const path = require("path");
const assert = require("assert");

const rootDir = path.resolve(__dirname, "..");
const libsDir = path.join(rootDir, "libs");
const pxtCorePath = path.resolve(rootDir, "../pxt/built/pxt.js");
const makerBaseBuilt = path.resolve(rootDir, "../pxt-maker/libs/base/built");
const makerTargetPath = path.resolve(rootDir, "../pxt-maker/pxtarget.json");

require(pxtCorePath);

function verifyEsp32C6Files() {
    const c6Dir = path.join(libsDir, "core---esp32c6");
    for (const f of [
        "pxt.json",
        "Makefile",
        "partitions.csv",
        "sdkconfig.defaults",
        "idf_component.yml",
        "dal.d.ts",
        "enums.d.ts",
        "shims.d.ts",
        "test.ts",
    ]) {
        assert.ok(fs.existsSync(path.join(c6Dir, f)), `missing core---esp32c6/${f}`);
    }

    const sdkconfig = fs.readFileSync(path.join(c6Dir, "sdkconfig.defaults"), "utf8");
    assert.ok(sdkconfig.includes('CONFIG_IDF_TARGET="esp32c6"'), "expected CONFIG_IDF_TARGET=\"esp32c6\"");
    assert.ok(sdkconfig.includes("CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y"), "expected USB_SERIAL_JTAG console");

    const idfComp = fs.readFileSync(path.join(c6Dir, "idf_component.yml"), "utf8");
    assert.ok(idfComp.includes("espressif/mdns"), "expected espressif/mdns dependency");

    const coreEsp32 = path.join(libsDir, "core---esp32");
    const platformH = fs.readFileSync(path.join(coreEsp32, "platform.h"), "utf8");
    assert.ok(platformH.includes("0x42000000"), "expected unified-MMU 0x42000000 in platform.h");
    const platformCpp = fs.readFileSync(path.join(coreEsp32, "platform.cpp"), "utf8");
    assert.ok(platformCpp.includes("CONFIG_IDF_TARGET_ESP32C6"), "expected ESP32-C6 guard in platform.cpp");
    const pintargetCpp = fs.readFileSync(path.join(coreEsp32, "pintarget.cpp"), "utf8");
    assert.ok(pintargetCpp.includes("adc_oneshot.h"), "expected IDF v5 adc_oneshot.h in pintarget.cpp");
    const pinsCpp = fs.readFileSync(path.join(coreEsp32, "pins.cpp"), "utf8");
    assert.ok(pinsCpp.includes("esp_task_wdt_config_t"), "expected IDF v5 WDT config in pins.cpp");
    const cryptoCpp = fs.readFileSync(path.join(coreEsp32, "crypto.cpp"), "utf8");
    assert.ok(cryptoCpp.includes("mbedtls_sha256_starts("), "expected mbedTLS v3 SHA-256 in crypto.cpp");

    const serialDir = path.join(libsDir, "serial---esp32");
    for (const f of ["pxt.json", "serial-target.h", "serial-target.cpp", "serial-target.ts", "shims.d.ts", "test.ts"]) {
        assert.ok(fs.existsSync(path.join(serialDir, f)), `missing serial---esp32/${f}`);
    }

    const wifiCpp = fs.readFileSync(path.join(libsDir, "wifi---esp32/wifi.cpp"), "utf8");
    assert.ok(wifiCpp.includes("setExternalAntenna"), "expected setExternalAntenna in wifi.cpp");
    console.log("✔ ESP32-C6 C++/IDF/package files verified in pxt-common-packages");
}

function loadPackageFiles(pkgDir, prefix, visited = new Set(), out = {}) {
    const abs = path.resolve(pkgDir);
    if (visited.has(abs)) return out;
    visited.add(abs);

    const cfgPath = path.join(abs, "pxt.json");
    if (!fs.existsSync(cfgPath)) return out;
    const cfg = JSON.parse(fs.readFileSync(cfgPath, "utf8"));

    const dirsToSearch = [abs];
    let curCfg = cfg;
    let curDir = abs;
    while (curCfg.additionalFilePath) {
        const addDir = path.resolve(curDir, curCfg.additionalFilePath);
        dirsToSearch.push(addDir);
        const addCfgPath = path.join(addDir, "pxt.json");
        if (fs.existsSync(addCfgPath)) {
            curCfg = JSON.parse(fs.readFileSync(addCfgPath, "utf8"));
            curDir = addDir;
            if (!cfg.dependencies && curCfg.dependencies) cfg.dependencies = curCfg.dependencies;
            if (!cfg.files && curCfg.files) cfg.files = curCfg.files;
        } else {
            break;
        }
    }
    if (!cfg.dependencies) cfg.dependencies = {};
    if (!cfg.files) cfg.files = [];
    out[prefix + "pxt.json"] = JSON.stringify(cfg);

    const wantedFiles = new Set(cfg.files || []);
    for (const d of dirsToSearch) {
        for (const fn of fs.readdirSync(d)) {
            if (fn === "pxt.json") continue;
            if (wantedFiles.size === 0 || wantedFiles.has(fn) || /\.(ts|d\.ts)$/.test(fn)) {
                const full = path.join(d, fn);
                if (fs.statSync(full).isFile() && !(prefix + fn in out)) {
                    out[prefix + fn] = fs.readFileSync(full, "utf8");
                }
            }
        }
    }

    const deps = Object.assign({}, cfg.dependencies || {});
    for (const dep of Object.keys(deps)) {
        const depSpec = deps[dep];
        if (depSpec.startsWith("file:")) {
            const rel = depSpec.slice(5);
            let depDir = path.resolve(abs, rel);
            if (!fs.existsSync(depDir)) {
                depDir = path.join(libsDir, dep);
            }
            loadPackageFiles(depDir, `pxt_modules/${dep}/`, visited, out);
        }
    }
    return out;
}

async function verifyCommonPackageTestsCompile() {
    const pxtarget = JSON.parse(fs.readFileSync(makerTargetPath, "utf8"));
    pxtarget.bundledpkgs = pxtarget.bundledpkgs || {};
    pxtarget.versions = pxtarget.versions || { target: "0.0.0", pxt: "0.0.0" };
    pxt.setAppTarget(pxtarget);

    const fileSystem = {};
    loadPackageFiles(path.join(libsDir, "base"), "pxt_modules/base/", new Set(), fileSystem);
    fileSystem["pxt_modules/base/pxt-core.d.ts"] = fs.readFileSync(path.join(makerBaseBuilt, "pxt-core.d.ts"), "utf8");
    fileSystem["pxt_modules/base/pxt-helpers.ts"] = fs.readFileSync(path.join(makerBaseBuilt, "pxt-helpers.ts"), "utf8");
    loadPackageFiles(path.join(libsDir, "core---esp32c6"), "pxt_modules/core/", new Set(), fileSystem);
    loadPackageFiles(path.join(libsDir, "serial---esp32"), "pxt_modules/serial/", new Set(), fileSystem);
    loadPackageFiles(path.join(libsDir, "wifi---esp32"), "pxt_modules/wifi/", new Set(), fileSystem);
    loadPackageFiles(path.join(libsDir, "net"), "pxt_modules/net/", new Set(), fileSystem);
    loadPackageFiles(path.join(libsDir, "mqtt"), "pxt_modules/mqtt/", new Set(), fileSystem);

    const coreTest = fs.readFileSync(path.join(libsDir, "core---esp32c6/test.ts"), "utf8");
    const serialTest = fs.readFileSync(path.join(libsDir, "serial---esp32/test.ts"), "utf8");
    const wifiTest = fs.readFileSync(path.join(libsDir, "wifi---esp32/test.ts"), "utf8");

    fileSystem["pxt.json"] = JSON.stringify({
        name: "pxt-common-esp32c6-tests",
        dependencies: {
            "core": "file:pxt_modules/core",
            "serial": "file:pxt_modules/serial",
            "wifi": "file:pxt_modules/wifi",
            "net": "file:pxt_modules/net",
            "mqtt": "file:pxt_modules/mqtt"
        },
        files: ["main.ts"]
    });
    fileSystem["main.ts"] = [coreTest, serialTest, wifiTest].join("\n\n");

    const host = {
        readFile: (pkg, fn) => {
            const p = pkg.id === "this" ? fn : `pxt_modules/${pkg.id}/${fn}`;
            return fileSystem[p] !== undefined ? fileSystem[p] : null;
        },
        writeFile: () => { },
        getHexInfoAsync: async () => ({ hex: [] }),
        cacheStoreAsync: async () => { },
        cacheGetAsync: async () => null,
        downloadPackageAsync: async () => { },
        resolveVersionAsync: async () => "*",
    };

    const mainPkg = new pxt.MainPackage(host);
    const opts = await mainPkg.getCompileOptionsAsync({ isNative: false, hasHex: false, switches: {} });
    const res = pxtc.compile(opts);
    if (!res.success) {
        console.error("Diagnostics:", JSON.stringify(res.diagnostics, null, 2));
    }
    assert.strictEqual(res.success, true, "expected core---esp32c6, serial---esp32, and wifi---esp32 tests to compile");
    assert.ok(res.outfiles[pxtc.BINARY_JS], "expected binary.js output");
    console.log("✔ core---esp32c6, serial---esp32, and wifi---esp32 test.ts suites compiled cleanly!");
}

(async () => {
    verifyEsp32C6Files();
    await verifyCommonPackageTestsCompile();
})().catch(err => {
    console.error(err);
    process.exit(1);
});

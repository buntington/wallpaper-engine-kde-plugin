// Runs Wallpaper Engine SceneScript property scripts (text clocks, dates, ...).
// A script is an ES module exporting init/update; it is rewritten into a
// function body so it can run in the QML JavaScript engine.

class Vec2 {
    constructor(x = 0, y = x) {
        if (typeof x === 'string') [x, y] = x.split(/\s+/).map(Number);
        else if (typeof x === 'object' && x !== null) ({ x = 0, y = 0 } = x);
        this.x = x; this.y = y;
    }
    copy() { return new Vec2(this.x, this.y); }
    add(v) { return typeof v === 'number' ? new Vec2(this.x + v, this.y + v) : new Vec2(this.x + v.x, this.y + v.y); }
    subtract(v) { return typeof v === 'number' ? new Vec2(this.x - v, this.y - v) : new Vec2(this.x - v.x, this.y - v.y); }
    multiply(v) { return typeof v === 'number' ? new Vec2(this.x * v, this.y * v) : new Vec2(this.x * v.x, this.y * v.y); }
    divide(v) { return typeof v === 'number' ? new Vec2(this.x / v, this.y / v) : new Vec2(this.x / v.x, this.y / v.y); }
    length() { return Math.hypot(this.x, this.y); }
    normalize() { const l = this.length() || 1; return this.divide(l); }
    dot(v) { return this.x * v.x + this.y * v.y; }
    mix(v, t) { return new Vec2(this.x + (v.x - this.x) * t, this.y + (v.y - this.y) * t); }
    toString() { return `${this.x} ${this.y}`; }
}

class Vec3 {
    constructor(x = 0, y = x, z = x) {
        if (typeof x === 'string') [x, y, z] = x.split(/\s+/).map(Number);
        else if (typeof x === 'object' && x !== null) ({ x = 0, y = 0, z = 0 } = x);
        this.x = x; this.y = y; this.z = z;
    }
    copy() { return new Vec3(this.x, this.y, this.z); }
    add(v) { return typeof v === 'number' ? new Vec3(this.x + v, this.y + v, this.z + v) : new Vec3(this.x + v.x, this.y + v.y, this.z + v.z); }
    subtract(v) { return typeof v === 'number' ? new Vec3(this.x - v, this.y - v, this.z - v) : new Vec3(this.x - v.x, this.y - v.y, this.z - v.z); }
    multiply(v) { return typeof v === 'number' ? new Vec3(this.x * v, this.y * v, this.z * v) : new Vec3(this.x * v.x, this.y * v.y, this.z * v.z); }
    divide(v) { return typeof v === 'number' ? new Vec3(this.x / v, this.y / v, this.z / v) : new Vec3(this.x / v.x, this.y / v.y, this.z / v.z); }
    length() { return Math.hypot(this.x, this.y, this.z); }
    normalize() { const l = this.length() || 1; return this.divide(l); }
    dot(v) { return this.x * v.x + this.y * v.y + this.z * v.z; }
    cross(v) { return new Vec3(this.y * v.z - this.z * v.y, this.z * v.x - this.x * v.z, this.x * v.y - this.y * v.x); }
    mix(v, t) { return new Vec3(this.x + (v.x - this.x) * t, this.y + (v.y - this.y) * t, this.z + (v.z - this.z) * t); }
    toString() { return `${this.x} ${this.y} ${this.z}`; }
}

const WEMath = {
    deg2rad: Math.PI / 180,
    rad2deg: 180 / Math.PI,
    mix: (a, b, t) => a + (b - a) * t,
    clamp: (v, lo, hi) => Math.min(Math.max(v, lo), hi),
    smoothStep: (lo, hi, v) => { const t = Math.min(Math.max((v - lo) / (hi - lo), 0), 1); return t * t * (3 - 2 * t); },
};

const WEColor = {
    rgb2hsv(c) {
        const max = Math.max(c.x, c.y, c.z), min = Math.min(c.x, c.y, c.z), d = max - min;
        let h = 0;
        if (d > 0) {
            if (max === c.x) h = ((c.y - c.z) / d) % 6;
            else if (max === c.y) h = (c.z - c.x) / d + 2;
            else h = (c.x - c.y) / d + 4;
            h /= 6; if (h < 0) h += 1;
        }
        return new Vec3(h, max === 0 ? 0 : d / max, max);
    },
    hsv2rgb(c) {
        const i = Math.floor(c.x * 6), f = c.x * 6 - i;
        const p = c.z * (1 - c.y), q = c.z * (1 - f * c.y), t = c.z * (1 - (1 - f) * c.y);
        return [new Vec3(c.z, t, p), new Vec3(q, c.z, p), new Vec3(p, c.z, t),
                new Vec3(p, q, c.z), new Vec3(t, p, c.z), new Vec3(c.z, p, q)][((i % 6) + 6) % 6];
    },
    normalizeColor: (c) => c.divide(255),
    expandColor: (c) => c.multiply(255),
};

const WEVector = { Vec2, Vec3 };

const modules = { WEMath, WEColor, WEVector };

// Builder behind `createScriptProperties()`; finish() yields the values,
// with the scene's saved values taking precedence over the declared defaults.
function makePropertiesBuilder(overrides) {
    const values = {};
    const builder = { finish: () => values };
    const add = (def) => {
        if (def && def.name !== undefined) {
            values[def.name] = Object.prototype.hasOwnProperty.call(overrides, def.name)
                ? overrides[def.name] : def.value;
        }
        return builder;
    };
    for (const kind of ['addCheckbox', 'addSlider', 'addCombo', 'addText', 'addTextInput',
                        'addColor', 'addFile', 'addDirectory'])
        builder[kind] = add;
    return builder;
}

// Rewrites module syntax into a plain function body that returns the exports.
export function transformSource(source) {
    // Imports become `var` so they may shadow the built-ins passed as parameters.
    let body = source.replace(
        /^\s*import\s+(?:\*\s+as\s+(\w+)|\{([^}]*)\}|(\w+))\s+from\s+['"]([^'"]+)['"];?/gm,
        (_, ns, named, def, mod) => {
            const src = `(__modules[${JSON.stringify(mod)}] || {})`;
            if (ns) return `var ${ns} = ${src};`;
            if (def) return `var ${def} = ${src}.default || ${src};`;
            const parts = named.split(',').map(s => s.trim()).filter(Boolean)
                .map(s => s.replace(/\s+as\s+/, ': '));
            return `var { ${parts.join(', ')} } = ${src};`;
        });
    body = body
        .replace(/^\s*export\s+default\s+/gm, 'const __default = ')
        .replace(/^(\s*)export\s+(async\s+function|function|class|var|let|const)\b/gm, '$1$2');
    const exportNames = ['init', 'update', 'destroy', 'cursorClick', 'cursorEnter', 'cursorLeave',
                         'cursorMove', 'cursorDown', 'cursorUp', 'applyUserProperties', 'scriptProperties'];
    const returns = exportNames.map(n => `${n}: typeof ${n} !== 'undefined' ? ${n} : undefined`).join(', ');
    return `${body}\n;return { ${returns} };`;
}

// Per scene; the location argument ("global" or "screen") selects a separate store.
function makeLocalStorage() {
    const stores = {};
    const store = (loc) => stores[loc || 'global'] || (stores[loc || 'global'] = {});
    return {
        LOCATION_GLOBAL: 'global',
        LOCATION_SCREEN: 'screen',
        get: (k, loc) => store(loc)[k],
        set: (k, v, loc) => { store(loc)[k] = v; },
        delete: (k, loc) => { delete store(loc)[k]; },
        clear: (loc) => { const s = store(loc); for (const k of Object.keys(s)) delete s[k]; },
    };
}

// Stand-in for a layer the runtime cannot drive, so scripts touching it keep running.
function makeStubLayer(name) {
    return {
        name, visible: true, alpha: 1, text: '',
        origin: new Vec3(), scale: new Vec3(1), angles: new Vec3(), size: new Vec2(),
        color: new Vec3(1), getParent: () => null, getChildren: () => [],
        play: () => {}, stop: () => {}, pause: () => {}, isPlaying: () => false,
        getAnimation: () => null, getTextureAnimation: () => null, getEffect: () => null,
    };
}

function makeScene(layers) {
    return {
        // Text layers are registered by name; anything else gets a stand-in.
        getLayer: (name) => layers[name] || (layers[name] = makeStubLayer(name)),
        enumerateLayers: () => Object.values(layers),
        getLayerIndex: () => -1,
        createLayer: (asset) => makeStubLayer(String(asset)),
        destroyLayer: () => {},
        sortLayer: () => {},
        getCameraTransforms: () => ({ eye: new Vec3(), center: new Vec3(0, 0, -1), up: new Vec3(0, 1, 0) }),
        getInitialLayerConfig: () => null,
    };
}

// Shared per scene: engine, input and `shared` objects every script sees.
export function createContext(opts) {
    const start = Date.now();
    const timers = [];
    // Buffers handed out by registerAudioBuffers, refreshed every tick.
    // opts.readAudio(n) returns n left values followed by n right values.
    const audioBuffers = [];
    const registerAudioBuffers = (n = 16) => {
        n = n >= 64 ? 64 : n >= 32 ? 32 : 16;
        const buf = { left: new Float32Array(n), right: new Float32Array(n), average: new Float32Array(n) };
        if (audioBuffers.length === 0 && opts.setAudio) opts.setAudio(true);
        audioBuffers.push(buf);
        return buf;
    };
    const engine = {
        frametime: 0,
        runtime: 0,
        timeOfDay: 0,
        userProperties: opts.userProperties || {},
        screenResolution: new Vec2(opts.width || 1920, opts.height || 1080),
        canvasSize: new Vec2(opts.orthoWidth || 1920, opts.orthoHeight || 1080),
        isDesktopDevice: () => true,
        isMobileDevice: () => false,
        isWallpaper: () => true,
        isScreensaver: () => false,
        isRunningInEditor: () => false,
        registerAudioBuffers,
        registerAsset: () => null,
        openUserShortcut: () => {},
        setTimeout: (fn, ms) => { timers.push({ fn, at: Date.now() + (ms || 0), every: 0 }); return timers.length; },
        setInterval: (fn, ms) => { timers.push({ fn, at: Date.now() + (ms || 0), every: Math.max(ms || 0, 1) }); return timers.length; },
        clearTimeout: (id) => { if (timers[id - 1]) timers[id - 1].fn = null; },
        clearInterval: (id) => { if (timers[id - 1]) timers[id - 1].fn = null; },
    };
    let last = Date.now();
    const layers = {};
    return {
        engine,
        layers,
        scene: makeScene(layers),
        localStorage: makeLocalStorage(),
        // Makes a layer object reachable through thisScene.getLayer(name).
        registerLayer(name, layer) { if (name && !(name in layers)) layers[name] = layer; },
        shared: {},
        input: { cursorWorldPosition: new Vec2(), cursorScreenPosition: new Vec2(), cursorLeftDown: false },
        tick() {
            const now = Date.now();
            engine.frametime = (now - last) / 1000;
            engine.runtime = (now - start) / 1000;
            const d = new Date(now);
            engine.timeOfDay = (d.getHours() * 3600 + d.getMinutes() * 60 + d.getSeconds()) / 86400;
            last = now;
            for (const t of timers) {
                if (!t.fn || now < t.at) continue;
                try { t.fn(); } catch (e) { console.warn('scenescript timer:', e); }
                if (t.every) t.at = now + t.every; else t.fn = null;
            }
            if (audioBuffers.length && opts.readAudio) {
                const read = {};
                for (const buf of audioBuffers) {
                    const n = buf.left.length;
                    const values = read[n] || (read[n] = opts.readAudio(n));
                    for (let i = 0; i < n; i++) {
                        buf.left[i] = values[i];
                        buf.right[i] = values[n + i];
                        buf.average[i] = (values[i] + values[n + i]) / 2;
                    }
                }
            }
        },
        // Stops audio capture if any script asked for it.
        close() {
            if (audioBuffers.length && opts.setAudio) opts.setAudio(false);
            audioBuffers.length = 0;
        },
    };
}

// Compiles one property script. Returns null if it cannot be compiled, so the
// caller falls back to the static value.
export function compile(source, scriptProperties, ctx, layer, name) {
    let exportsObj;
    try {
        const factory = new Function(
            'engine', 'thisLayer', 'thisObject', 'thisScene', 'input', 'shared', 'localStorage',
            'createScriptProperties', '__modules', 'Vec2', 'Vec3', 'WEMath', 'WEColor', 'WEVector',
            transformSource(source));
        exportsObj = factory(
            ctx.engine, layer, layer, ctx.scene, ctx.input, ctx.shared, ctx.localStorage,
            () => makePropertiesBuilder(scriptProperties || {}), modules,
            Vec2, Vec3, WEMath, WEColor, WEVector);
    } catch (e) {
        console.warn(`scenescript: cannot compile script for '${name}':`, e);
        return null;
    }
    let initialized = false;
    let failed = false;
    return {
        // Runs init on first use, then update; returns the new value.
        run(value) {
            if (failed) return value;
            try {
                if (!initialized) {
                    initialized = true;
                    if (typeof exportsObj.init === 'function') {
                        const v = exportsObj.init(value);
                        if (v !== undefined) value = v;
                    }
                }
                if (typeof exportsObj.update === 'function') {
                    const v = exportsObj.update(value);
                    if (v !== undefined) value = v;
                }
            } catch (e) {
                failed = true;
                console.warn(`scenescript: script for '${name}' failed:`, e);
            }
            return value;
        },
        get animated() { return typeof exportsObj.update === 'function'; },
    };
}

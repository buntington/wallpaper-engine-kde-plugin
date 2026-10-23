import QtQuick 2.15
import ".."
import "../js/scenescript.mjs" as SceneScript

// Runs the scripts of a scene's text objects (clocks, dates, ...) and sends
// their text to the renderer, which draws text objects as scene layers.
Item {
    id: textRoot
    anchors.fill: parent

    property var sceneText: null    // (source, assets) => Promise of scene info
    property var player: null       // SceneViewer
    property string source
    property string assets
    property int displayMode: Common.DisplayMode.Crop
    property bool playing: true

    property var info: null
    property var ctx: null
    property var layers: []
    property bool scripted: false
    property int retries: 0

    // Emitted once the first text values are known (or loading gave up):
    // the scene should be loaded now so it starts with the right text.
    signal ready(var textValues, var userProperties)

    readonly property real orthoW: info ? info.ortho[0] : 1920
    readonly property real orthoH: info ? info.ortho[1] : 1080
    // Item pixels per scene unit, matching the renderer's fill modes.
    readonly property real scaleX: {
        const sx = width / orthoW, sy = height / orthoH;
        if (displayMode == Common.DisplayMode.Scale) return sx;
        if (displayMode == Common.DisplayMode.Aspect) return Math.min(sx, sy);
        return Math.max(sx, sy);
    }
    readonly property real scaleY: {
        const sx = width / orthoW, sy = height / orthoH;
        if (displayMode == Common.DisplayMode.Scale) return sy;
        if (displayMode == Common.DisplayMode.Aspect) return Math.min(sx, sy);
        return Math.max(sx, sy);
    }

    onSourceChanged: load()
    Component.onCompleted: load()

    Component.onDestruction: if (ctx) ctx.close()

    function load() {
        if (ctx) ctx.close();
        info = null;
        ctx = null;
        layers = [];
        scripted = false;
        if (!source) return;
        if (!sceneText) {
            ready({}, {});
            return;
        }
        const requested = source;
        giveUpTimer.restart();
        sceneText(source, assets).then((res) => {
            if (requested !== textRoot.source || !res) return;
            giveUpTimer.stop();
            textRoot.retries = 0;
            textRoot.start(res);
        }).catch((e) => {
            // The python helper may still be starting.
            if (requested !== textRoot.source) return;
            if (textRoot.retries++ < 5) retryTimer.start();
            else console.error("scene text:", e);
        });
    }

    function start(res) {
        const c = SceneScript.createContext({
            userProperties: res.userProperties,
            width: width, height: height,
            orthoWidth: res.ortho[0], orthoHeight: res.ortho[1],
            readAudio: (n) => player ? player.audioSpectrum(n) : new Array(n * 2).fill(0),
            setAudio: (on) => { if (player) player.setAudioCapture(on); },
        });
        c.tick();
        // Every text layer is reachable through thisScene.getLayer(), so register
        // them all before any script runs.
        const list = res.texts.map(obj => {
            const js = { name: obj.name, text: obj.text.value, visible: obj.visible, alpha: 1.0 };
            c.registerLayer(obj.name, js);
            return { id: obj.id, value: obj.text.value, script: null, js: js, obj: obj };
        });
        for (const layer of list) {
            // Hidden objects are not drawn, so their scripts need not run.
            if (layer.obj.visible && layer.obj.text.script)
                layer.script = SceneScript.compile(layer.obj.text.script, layer.obj.text.scriptproperties,
                                                   c, layer.js, layer.obj.name);
            if (layer.script) {
                const v = layer.script.run(layer.value);
                if (v !== undefined && v !== null) layer.js.text = String(v);
            }
        }
        const values = {};
        for (const layer of list) {
            layer.value = String(layer.js.text);
            values[String(layer.id)] = layer.value;
        }
        ctx = c;
        layers = list;
        scripted = list.some(l => l.script);
        info = res;
        ready(values, res.userProperties || {});
    }

    function tick() {
        ctx.tick();
        for (const layer of layers) {
            if (!layer.script) continue;
            const v = layer.script.run(layer.value);
            if (v !== undefined && v !== null) layer.js.text = String(v);
        }
        // Scripts may also set the text of other layers through thisScene.getLayer().
        for (const layer of layers) {
            const s = String(layer.js.text);
            if (s === layer.value) continue;
            layer.value = s;
            if (player) player.setLayerText(layer.id, s);
        }
    }

    Timer {
        id: retryTimer
        interval: 2000
        onTriggered: textRoot.load()
    }

    // Load the scene without text values if the helper does not answer.
    Timer {
        id: giveUpTimer
        interval: 4000
        onTriggered: textRoot.ready({}, {})
    }

    Timer {
        interval: 100
        repeat: true
        running: textRoot.playing && textRoot.ctx !== null && textRoot.scripted
        onTriggered: textRoot.tick()
    }

    // Cursor in scene coordinates for scripts; passive, so the scene keeps its mouse input.
    HoverHandler {
        onPointChanged: {
            if (!textRoot.ctx) return;
            const p = point.position;
            const input = textRoot.ctx.input;
            input.cursorScreenPosition.x = p.x;
            input.cursorScreenPosition.y = p.y;
            input.cursorWorldPosition.x = (p.x - textRoot.width / 2) / textRoot.scaleX + textRoot.orthoW / 2;
            input.cursorWorldPosition.y = textRoot.orthoH / 2 - (p.y - textRoot.height / 2) / textRoot.scaleY;
        }
    }
    PointHandler {
        acceptedButtons: Qt.LeftButton
        onActiveChanged: if (textRoot.ctx) textRoot.ctx.input.cursorLeftDown = active
    }
}

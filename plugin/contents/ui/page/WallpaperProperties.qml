import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs
import QtQuick.Layouts 1.15

import ".."
import "../components"
import "../js/utils.mjs" as Utils

// The wallpaper's own options from its project.json ("user properties").
// Values are saved per wallpaper under "user_properties".
OptionGroup {
    id: root

    property string projectPath     // url of project.json
    property var readfile: null     // (path) => Promise of file text
    property var options: null      // the per-wallpaper option group that saves changes

    property var props: []          // [{name, type, label, ...}] in display order
    property var defaults: ({})
    property var values: ({})       // defaults overlaid with saved and pending changes

    visible: props.length > 0

    header.text: 'Wallpaper Properties'
    header.text_color: Theme.textColor
    header.icon: '../../images/cheveron-down.svg'
    header.color: Theme.activeBackgroundColor

    onProjectPathChanged: load()
    Component.onCompleted: load()
    // OptionGroup's children must be items, so the watcher hangs off a property.
    property Connections _optionsWatch: Connections {
        target: root.options
        function onConfigChanged() { root.refreshValues(); }
    }

    function label(text, localization) {
        if (!text) return '';
        if (localization && localization[text]) text = localization[text];
        text = String(text).replace(/<[^>]*>/g, '').trim();
        const m = text.match(/^ui_(?:browse|editor)_properties_(.*)$/);
        if (m) text = m[1].replace(/_/g, ' ').replace(/^\w/, c => c.toUpperCase());
        return text;
    }

    function load() {
        props = [];
        if (!projectPath || !readfile) return;
        const path = projectPath;
        readfile(Common.urlNative(path)).then(value => {
            if (path !== root.projectPath) return;
            const project = Utils.parseJson(value);
            const general = project && project.general ? project.general : {};
            const loc = general.localization ? (general.localization['en-us'] || {}) : {};
            const list = [];
            const defs = {};
            for (const [name, p] of Object.entries(general.properties || {})) {
                if (!p || typeof p !== 'object') continue;
                const type = String(p.type || '').toLowerCase();
                if (!['bool', 'slider', 'combo', 'color', 'textinput', 'text'].includes(type)) continue;
                if (type !== 'text') defs[name] = p.value;
                list.push({
                    name: name,
                    type: type,
                    label: label(p.text, loc) || name,
                    order: Number(p.order) || 0,
                    condition: p.condition || '',
                    min: Number(p.min ?? 0),
                    max: Number(p.max ?? 100),
                    step: Number(p.step) || (p.fraction ? 0.01 : 1),
                    precision: p.fraction === false ? 0 : Number(p.precision ?? (p.fraction ? 2 : 0)),
                    options: (p.options || []).map(o => ({ text: label(o.label, loc), value: o.value })),
                });
            }
            list.sort((a, b) => a.order - b.order);
            root.defaults = defs;
            root.props = list;
            root.refreshValues();
        }).catch(reason => console.error(`read '${path}' error\n`, reason));
    }

    function refreshValues() {
        const saved = options ? options.get_config_val('user_properties') : null;
        values = Object.assign({}, defaults, saved || {});
    }

    function setValue(name, value) {
        if (values[name] === value) return;
        const next = Object.assign({}, values);
        next[name] = value;
        values = next;
        // Only keep values that differ from the wallpaper's defaults.
        const changed = {};
        for (const [k, v] of Object.entries(next))
            if (JSON.stringify(v) !== JSON.stringify(defaults[k])) changed[k] = v;
        if (options) options.set_config('user_properties', changed);
    }

    // Wallpaper Engine conditions are JS expressions such as "showclock.value == true".
    function conditionMet(condition) {
        if (!condition) return true;
        try {
            const names = Object.keys(values).filter(n => /^[A-Za-z_$][\w$]*$/.test(n));
            const fn = new Function(...names, `return (${condition});`);
            return !!fn(...names.map(n => ({ value: values[n] })));
        } catch (e) {
            return true;
        }
    }

    // Some wallpapers store booleans as strings or numbers.
    function toBool(v) {
        return v === true || v === 1 || v === 'true' || v === '1';
    }

    function colorFromString(s) {
        const c = String(s || '1 1 1').trim().split(/\s+/).map(Number);
        const v = (i) => Math.max(0, Math.min(1, isNaN(c[i]) ? 1 : c[i]));
        return Qt.rgba(v(0), v(1), v(2), 1);
    }
    function colorToString(c) {
        return [c.r, c.g, c.b].map(x => x.toFixed(5)).join(' ');
    }

    Repeater {
        model: root.props
        OptionItem {
            id: row
            readonly property var prop: modelData
            text: row.prop.label
            text_color: Theme.textColor
            visible: root.conditionMet(row.prop.condition)
            actor: Loader {
                active: row.prop.type !== 'text'
                sourceComponent: ({
                    bool: boolControl, slider: sliderControl, combo: comboControl,
                    color: colorControl, textinput: textControl,
                })[row.prop.type] || null
            }

            Component {
                id: boolControl
                Switch {
                    checked: root.toBool(root.values[row.prop.name])
                    onToggled: root.setValue(row.prop.name, checked)
                }
            }
            Component {
                id: sliderControl
                RowLayout {
                    Slider {
                        id: slider
                        Layout.preferredWidth: 160
                        from: row.prop.min
                        to: row.prop.max
                        stepSize: row.prop.step
                        value: Number(root.values[row.prop.name] ?? row.prop.min)
                        onMoved: root.setValue(row.prop.name, Number(value.toFixed(row.prop.precision)))
                    }
                    Label {
                        Layout.preferredWidth: 48
                        text: slider.value.toFixed(row.prop.precision)
                        color: Theme.textColor
                    }
                }
            }
            Component {
                id: comboControl
                ComboBox {
                    model: row.prop.options
                    textRole: 'text'
                    valueRole: 'value'
                    currentIndex: Math.max(0, row.prop.options.findIndex(o => String(o.value) === String(root.values[row.prop.name])))
                    onActivated: root.setValue(row.prop.name, row.prop.options[currentIndex].value)
                }
            }
            Component {
                id: colorControl
                Button {
                    implicitWidth: 64
                    contentItem: Rectangle {
                        implicitHeight: 20
                        radius: 3
                        color: root.colorFromString(root.values[row.prop.name])
                        border.color: Theme.textColor
                    }
                    onClicked: colorDialog.open()
                    ColorDialog {
                        id: colorDialog
                        selectedColor: root.colorFromString(root.values[row.prop.name])
                        onAccepted: root.setValue(row.prop.name, root.colorToString(selectedColor))
                    }
                }
            }
            Component {
                id: textControl
                TextField {
                    implicitWidth: 180
                    text: String(root.values[row.prop.name] ?? '')
                    onEditingFinished: root.setValue(row.prop.name, text)
                }
            }
        }
    }
}

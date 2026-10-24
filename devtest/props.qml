import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import org.kde.plasma.components 3.0 as PlasmaComponents
import "../plugin/contents/ui/page"

// The "Wallpaper Properties" tab for one wallpaper, screenshot after a moment.
// Run it in a headless compositor so no window shows up, e.g.
//   kwin_wayland --virtual --socket wek-test --exit-with-session \
//     "env QT_QPA_PLATFORM=wayland WAYLAND_DISPLAY=wek-test QML_XHR_ALLOW_FILE_READ=1 \
//      qml6 devtest/props.qml -- <out.png> <wallpaper dir> <title>"
Window {
    id: win
    width: 640; height: 820; visible: true
    readonly property var args: Qt.application.arguments

    QtObject {
        id: fakeOptions
        signal configChanged()
        function get_config_val(key) { return null; }
        function set_config(key, value) {}
    }

    // grabToImage leaves out the window color
    Rectangle { anchors.fill: parent; color: palette.window }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 5

        // the tab bar of the wallpaper settings, as in config.qml
        PlasmaComponents.TabBar {
            currentIndex: 1
            implicitWidth: font.pixelSize * 8 * 4
            PlasmaComponents.TabButton { text: "Wallpapers" }
            PlasmaComponents.TabButton { text: "Properties" }
            PlasmaComponents.TabButton { text: "Settings" }
            PlasmaComponents.TabButton { text: "About" }
        }

        PropertiesPage {
            Layout.fillWidth: true
            Layout.fillHeight: true
            wallpaper: ({
                workshopid: "test", type: "scene",
                path: "file://" + win.args[win.args.length - 2],
                title: win.args[win.args.length - 1]
            })
            options: fakeOptions
            readfile: path => new Promise(resolve => {
                const xhr = new XMLHttpRequest();
                xhr.onreadystatechange = () => {
                    if (xhr.readyState === XMLHttpRequest.DONE) resolve(xhr.responseText);
                };
                xhr.open("GET", "file://" + path);
                xhr.send();
            })
        }
    }

    Timer {
        interval: 1500; running: true
        onTriggered: win.contentItem.grabToImage(r => { r.saveToFile(win.args[win.args.length - 3]); Qt.quit(); })
    }
}

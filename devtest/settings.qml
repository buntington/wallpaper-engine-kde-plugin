import QtQuick
import QtQuick.Window
import "../plugin/contents/ui/page"

// The plugin's Settings tab, screenshot after a moment (theme comes from the platform).
// Usage: QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=kde qml6 devtest/settings.qml -- <out.png>
Window {
    id: win
    width: 780; height: 900; visible: true
    readonly property var args: Qt.application.arguments
    property QtObject libcheck: QtObject { property bool wallpaper: true }
    property int cfg_PauseMode: 0
    property int cfg_DisplayMode: 0
    property int cfg_VideoBackend: 0
    Rectangle { anchors.fill: parent; color: palette.window }
    SettingPage { anchors.fill: parent }
    Timer {
        interval: 1500; running: true
        onTriggered: win.contentItem.grabToImage(r => { r.saveToFile(win.args[win.args.length - 1]); Qt.quit(); })
    }
}

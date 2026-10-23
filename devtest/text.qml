import QtQuick
import QtQuick.Window
import com.github.catsout.wallpaperEngineKde 1.2
import "../plugin/contents/ui/backend"
import "scene-data.js" as Data

// Renderer + text script driver, fed with scene_text() output from devtest/scene-data.js.
Window {
    id: win
    width: 1280; height: 720; visible: true; color: "black"
    readonly property var args: Qt.application.arguments
    SceneViewer {
        id: player
        anchors.fill: parent
        fillMode: SceneViewer.ASPECTCROP
        muted: true
        assets: "file://" + win.args[win.args.length - 1]
        Component.onCompleted: play()
    }
    SceneText {
        player: player
        sceneText: (s, a) => Promise.resolve(Data.data)
        source: "file://" + win.args[win.args.length - 2]
        onReady: (values, props) => {
            player.textValues = values;
            player.userProperties = props;
            player.source = source;
        }
    }
    Timer {
        interval: 6000; running: true
        onTriggered: win.contentItem.grabToImage(r => { r.saveToFile(win.args[win.args.length - 3]); Qt.quit(); })
    }
}

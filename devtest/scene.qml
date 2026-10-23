import QtQuick
import QtQuick.Window
import com.github.catsout.wallpaperEngineKde 1.2

Window {
    id: win
    width: 1280; height: 720; visible: true; color: "black"
    title: "scene devtest"
    SceneViewer {
        id: player
        anchors.fill: parent
        fillMode: SceneViewer.ASPECTCROP
        muted: true
        assets: "file://" + Qt.application.arguments[Qt.application.arguments.length - 1] 
        source: "file://" + Qt.application.arguments[Qt.application.arguments.length - 2]
        Component.onCompleted: play()
    }
    Timer {
        interval: 6000; running: true
        onTriggered: win.contentItem.grabToImage(r => { r.saveToFile(Qt.application.arguments[Qt.application.arguments.length - 3]); Qt.quit(); })
    }
}

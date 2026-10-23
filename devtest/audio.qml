import QtQuick
import QtQuick.Window
import com.github.catsout.wallpaperEngineKde 1.2

// Prints the captured spectrum (16 bands, left then right) a few times.
Window {
    id: win
    width: 200; height: 100; visible: true
    SceneViewer {
        id: player
        anchors.fill: parent
        Component.onCompleted: player.setAudioCapture(true)
    }
    property int n: 0
    Timer {
        interval: 1000; repeat: true; running: true
        onTriggered: {
            const v = player.audioSpectrum(16).map(x => x.toFixed(2));
            console.log("L", v.slice(0, 16).join(" "), "| R", v.slice(16).join(" "));
            if (++win.n >= 4) Qt.quit();
        }
    }
}

import QtQuick 2.5
import com.github.catsout.wallpaperEngineKde 1.2
import ".."

Item{
    id: sceneItem
    anchors.fill: parent
    // Passed to the player once text scripts and user properties are ready.
    property string source
    property string assets: "assets"
    property var sceneText: null
    property int displayMode: background.displayMode
    property var volumeFade: Common.createVolumeFade(
        sceneItem, 
        Qt.binding(function() { return background.mute ? 0 : background.volume; }),
        (volume) => { player.volume = volume / 100.0; }
    )

    onDisplayModeChanged: {
        if(displayMode == Common.DisplayMode.Scale)
            player.fillMode = SceneViewer.STRETCH;
        else if(displayMode == Common.DisplayMode.Aspect)
            player.fillMode = SceneViewer.ASPECTFIT;
        else if(displayMode == Common.DisplayMode.Crop)
            player.fillMode = SceneViewer.ASPECTCROP;
    }

    SceneViewer {
        id: player
        anchors.fill: parent
        fps: background.fps
        muted: background.mute
        speed: background.speed
        assets: sceneItem.assets
        Component.onCompleted: {
            player.setAcceptMouse(true);
            player.setAcceptHover(true);
        }

        Connections {
            target: player
            function onFirstFrame() {
                background.sig_backendFirstFrame('scene');
            }
        }
    }

    SceneText {
        id: textOverlay
        player: player
        sceneText: sceneItem.sceneText
        assets: sceneItem.assets
        source: sceneItem.source
        displayMode: sceneItem.displayMode
        onReady: (textValues, userProperties) => {
            player.textValues = textValues;
            player.userProperties = userProperties;
            player.source = sceneItem.source;
        }
    }

    // Changed wallpaper options are saved by the config page; reload them.
    Connections {
        target: background
        function onCurOptChanged() { textOverlay.load(); }
    }

    Component.onCompleted: {
        background.nowBackend = 'scene';
        sceneItem.displayModeChanged();
    }
    function play() {
        volumeFade.start();
        player.play();
        textOverlay.playing = true;
    }
    function pause() {
        volumeFade.stop();
        player.pause();
        textOverlay.playing = false;
    }
    
    function getMouseTarget() {
        return Qt.binding(function() { return player; })
    }
}

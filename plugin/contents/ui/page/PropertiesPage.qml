import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.5

import ".."
import "../components"

// The selected wallpaper's own options (its project.json "user properties").
Flickable {
    id: root

    property var wallpaper: null    // the wallpaper selected in the Wallpapers tab
    property var options: null      // its per-wallpaper option group, saves changes
    property var readfile: null

    readonly property bool isScene: Boolean(wallpaper && wallpaper.workshopid) && wallpaper.type === 'scene'

    Layout.fillWidth: true
    ScrollBar.vertical: ScrollBar { id: scrollbar }

    contentWidth: width - scrollbar.width
    contentHeight: column.implicitHeight
    clip: true
    boundsBehavior: Flickable.OvershootBounds

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 8

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.leftMargin: 16
            visible: Boolean(root.wallpaper && root.wallpaper.title)
            text: root.wallpaper ? root.wallpaper.title : ''
            color: Theme.textColor
            font.bold: true
            font.pointSize: Math.round(Qt.application.font.pointSize * 1.3)
            elide: Text.ElideRight
        }

        WallpaperProperties {
            id: properties
            Layout.fillWidth: true
            projectPath: root.isScene ? Common.getWpModelProjectPath(root.wallpaper) : ''
            readfile: root.readfile
            options: root.options
        }

        Label {
            Layout.fillWidth: true
            Layout.margins: 16
            visible: properties.props.length === 0
            wrapMode: Text.WordWrap
            color: Theme.disabledTextColor
            text: !root.wallpaper || !root.wallpaper.workshopid
                ? 'Pick a wallpaper in the Wallpapers tab to see its options here.'
                : !root.isScene
                ? 'Only scene wallpapers have options here.'
                : "This wallpaper doesn't have any options."
        }
    }
}

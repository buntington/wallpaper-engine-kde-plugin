pragma Singleton
import QtQuick 2.0

// Colors from the system palette, which follows the KDE color scheme. Kirigami's
// attached theme never resolves in this singleton (it has no window), which left
// every color black in Plasma 6.
Item {
    id: root_item

    SystemPalette { id: pal; colorGroup: SystemPalette.Active }
    SystemPalette { id: pal_disabled; colorGroup: SystemPalette.Disabled }

    readonly property color textColor:                pal.windowText
    readonly property color highlightColor:           pal.highlight
    readonly property color highlightedTextColor:     pal.highlightedText
    readonly property color backgroundColor:          pal.window
    readonly property color activeBackgroundColor:    Qt.tint(pal.window, Qt.alpha(pal.highlight, 0.25))
    readonly property color alternateBackgroundColor: pal.alternateBase
    readonly property color linkColor:                pal.highlight
    readonly property color visitedLinkColor:         Qt.darker(pal.highlight, 1.3)
    readonly property color positiveTextColor:        "#27ae60"
    readonly property color positiveBackgroundColor:  Qt.tint(pal.window, Qt.alpha(positiveTextColor, 0.3))
    readonly property color neutralTextColor:         "#f67400"
    readonly property color negativeTextColor:        "#da4453"
    readonly property color disabledTextColor:        pal_disabled.windowText

    // the same for content areas (lists, panels)
    readonly property QtObject view: QtObject {
        readonly property color textColor:                pal.text
        readonly property color highlightColor:           pal.highlight
        readonly property color highlightedTextColor:     pal.highlightedText
        readonly property color backgroundColor:          pal.base
        readonly property color activeBackgroundColor:    Qt.tint(pal.base, Qt.alpha(pal.highlight, 0.25))
        readonly property color alternateBackgroundColor: pal.alternateBase
        readonly property color linkColor:                pal.highlight
        readonly property color visitedLinkColor:         Qt.darker(pal.highlight, 1.3)
        readonly property color positiveTextColor:        root_item.positiveTextColor
        readonly property color positiveBackgroundColor:  Qt.tint(pal.base, Qt.alpha(positiveTextColor, 0.3))
        readonly property color neutralTextColor:         root_item.neutralTextColor
        readonly property color negativeTextColor:        root_item.negativeTextColor
        readonly property color disabledTextColor:        pal_disabled.text
    }
}

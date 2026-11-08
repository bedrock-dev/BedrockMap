import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    required property int orientation

    readonly property bool horizontal: orientation === Qt.Horizontal
    implicitWidth: Theme.gapSmall
    implicitHeight: Theme.gapSmall
    color: "transparent"

    HoverHandler {
        cursorShape: root.horizontal ? Qt.SplitHCursor : Qt.SplitVCursor
    }

    Rectangle {
        anchors.centerIn: parent
        width: root.horizontal ? Math.max(1, Theme.ui(3)) : Math.max(0, parent.width - Theme.gapSmall * 2)
        height: root.horizontal ? Math.max(0, parent.height - Theme.gapSmall * 2) : Math.max(1, Theme.ui(3))
        radius: Math.min(width, height) / 2
        color: SplitHandle.pressed || SplitHandle.hovered ? Theme.accent : Theme.border
    }
}

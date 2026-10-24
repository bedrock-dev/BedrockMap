import BedrockMap 1.0
import QtQuick

Rectangle {
    id: root
    property string label
    property bool active: false
    property bool compact: false
    signal toggled

    width: Math.max(compact ? 54 : 68, labelText.implicitWidth + (Theme.touch ? 20 : (compact ? 14 : 20)))
    height: Theme.touch ? 40 : (compact ? 24 : Theme.controlHeight)
    radius: Theme.radiusControl
    color: active || toggleMouse.pressed ? Theme.accentSoft : Theme.surfaceAlt
    border.width: 1
    border.color: active || toggleMouse.pressed ? Theme.accent : Theme.border

    Text {
        id: labelText
        anchors.centerIn: parent
        text: root.label
        color: root.active ? Theme.accent : Theme.muted
        font.pixelSize: root.compact ? Theme.fontCaption : Theme.fontBody
    }

    MouseArea {
        id: toggleMouse
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.active = !root.active
            root.toggled()
        }
    }
}

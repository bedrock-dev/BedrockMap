import BedrockMap 1.0
import QtQuick

Item {
    id: root
    property string label
    property bool active: false
    property bool compact: false
    signal toggled

    implicitWidth: Math.max(Theme.ui(compact ? 54 : 68), labelText.implicitWidth + (Theme.touch ? Theme.controlPadding
                                                                                                  * 2 : (compact ? 14 :
                                                                                                                   20)))
    implicitHeight: Theme.touch ? Theme.controlHeight : (compact ? 24 : Theme.controlHeight)
    width: implicitWidth
    height: implicitHeight

    Rectangle {
        anchors.centerIn: parent
        width: parent.width
        height: Theme.touch ? Math.min(root.height, Theme.controlVisualHeight) : root.height
        radius: Theme.radiusControl
        color: root.active || toggleMouse.pressed ? Theme.accentSoft : Theme.surfaceAlt
        border.width: Theme.borderWidth
        border.color: root.active || toggleMouse.pressed ? Theme.accent : Theme.border
    }

    Text {
        id: labelText
        anchors.centerIn: parent
        width: Math.max(0, root.width - Theme.controlPadding * 2)
        text: root.label
        color: root.active ? Theme.accent : Theme.muted
        font.pixelSize: root.compact ? Theme.fontCaption : Theme.fontBody
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
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

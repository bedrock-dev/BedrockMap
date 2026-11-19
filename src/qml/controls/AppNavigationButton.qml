import BedrockMap 1.0
import QtQuick

Rectangle {
    id: root
    property string icon
    property string iconName
    property string label
    property bool selected: false
    signal clicked

    readonly property color activeColor: root.selected ? Theme.accent : Theme.muted
    readonly property color backgroundColor: {
        if (root.selected || mouse.pressed)
            return Theme.accentSoft
        return mouse.containsMouse ? Theme.surfaceAlt : "transparent"
    }

    width: parent.width
    height: parent.height
    radius: Theme.radiusCard
    color: root.backgroundColor

    Column {
        anchors.centerIn: parent
        width: parent.width
        spacing: Theme.gapTiny
        AppIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.iconSize
            height: width
            name: root.iconName
            fallback: root.icon
            iconSize: width
            iconColor: root.activeColor
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.max(0, parent.width - Theme.gapSmall)
            text: root.label
            color: root.activeColor
            font.pixelSize: Theme.fontCaption
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    Accessible.role: Accessible.Button
    Accessible.name: label
}

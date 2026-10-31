import BedrockMap 1.0
import QtQuick

Rectangle {
    id: root
    property string icon
    property string iconName
    property string label
    property bool selected: false
    signal clicked

    width: parent.width
    height: parent.height
    radius: Theme.radiusCard
    color: selected || mouse.pressed ? Theme.accentSoft : (mouse.containsMouse ? Theme.surfaceAlt : "transparent")

    Column {
        anchors.centerIn: parent
        width: parent.width
        spacing: Theme.ui(2)
        AppIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.iconSize
            height: width
            name: root.iconName
            fallback: root.icon
            iconSize: width
            iconColor: root.selected || mouse.pressed ? Theme.accent : Theme.muted
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.max(0, parent.width - Theme.gapSmall)
            text: root.label
            color: root.selected || mouse.pressed ? Theme.accent : Theme.muted
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

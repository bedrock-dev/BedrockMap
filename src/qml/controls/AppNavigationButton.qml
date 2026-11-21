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
    readonly property bool hot: mouse.pressed || mouse.containsMouse
    readonly property real iconSlotSize: Theme.touch ? Theme.ui(40) : Theme.ui(34)

    width: parent.width
    height: parent.height
    color: "transparent"

    Column {
        anchors.centerIn: parent
        width: parent.width
        spacing: Theme.gapTiny
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.iconSlotSize
            height: width
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: root.hot && !root.selected ? Theme.surfaceAlt : "transparent"
            }
            AppIcon {
                anchors.centerIn: parent
                width: Theme.iconSize
                height: width
                name: root.iconName
                fallback: root.icon
                iconSize: width
                iconColor: root.activeColor
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.max(0, parent.width - Theme.gapSmall)
            text: root.label
            color: Theme.muted
            font.pixelSize: Theme.fontSmall
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

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

AbstractButton {
    id: root
    property string fallbackIcon
    property string iconName
    property string label
    property bool selected: false

    readonly property color activeColor: root.selected ? Theme.accent : Theme.muted
    readonly property bool hot: pressed || hovered
    readonly property real iconSlotSize: Theme.touch ? Theme.ui(40) : Theme.ui(34)

    width: parent.width
    height: parent.height
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.TabFocus

    Keys.onReturnPressed: animateClick()
    Keys.onEnterPressed: animateClick()

    background: Rectangle {
        radius: Theme.radiusMedium
        color: root.visualFocus ? Theme.accentSoft : "transparent"
        border.width: root.visualFocus ? Theme.borderWidth * 3 : 0
        border.color: Theme.accent
    }

    contentItem: Column {
        width: parent.width
        anchors.centerIn: parent
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
                fallback: root.fallbackIcon
                iconSize: width
                iconColor: root.activeColor
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.max(0, parent.width - Theme.gapSmall)
            text: root.label
            color: root.selected ? Theme.accent : Theme.muted
            font.pixelSize: Theme.fontSmall
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    Accessible.name: root.label
    Accessible.checkable: true
    Accessible.checked: root.selected
}

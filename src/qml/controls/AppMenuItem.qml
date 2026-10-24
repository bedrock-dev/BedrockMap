import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

MenuItem {
    id: root
    property bool danger: false
    property string iconName

    readonly property real rowHeight: Theme.touch ? Theme.touchTarget : 30

    // Submenu rows are generated from the menu delegate and may fall back to
    // implicit content sizing. Keep platform padding and spacing out of that
    // calculation so every row has exactly the same height and alignment.
    leftPadding: 0
    topPadding: 0
    rightPadding: 0
    bottomPadding: 0
    spacing: 0
    implicitHeight: visible ? rowHeight : 0
    height: visible ? rowHeight : 0
    implicitWidth: visible ? Math.max(160, contentItem ? contentItem.implicitWidth + 20 : 160) : 0
    width: visible && parent ? parent.width : 0

    contentItem: Item {
        implicitWidth: Math.max(160, label.implicitWidth + 20 + (root.iconName.length > 0 ? 24 : 0)
                                       + (root.subMenu ? 14 : 0))
        implicitHeight: root.rowHeight
        AppIcon {
            visible: root.iconName.length > 0
            width: 16
            height: 16
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            name: root.iconName
            iconSize: width
            iconColor: root.danger ? Theme.danger : (root.highlighted ? Theme.accent : Theme.muted)
        }
        Text {
            id: label
            anchors.left: parent.left
            anchors.leftMargin: 10 + (root.iconName.length > 0 ? 24 : 0)
            anchors.right: parent.right
            anchors.rightMargin: root.subMenu ? 24 : 10
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.danger ? Theme.danger : Theme.text
            font.pixelSize: Theme.fontBody
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            visible: root.subMenu !== null
            text: "›"
            color: Theme.muted
            font.pixelSize: Theme.fontBody
        }
    }
    arrow: Item {
        width: 0
        height: 0
        implicitWidth: 0
        implicitHeight: 0
    }
    indicator: Item {
        width: 0
        height: 0
        implicitWidth: 0
        implicitHeight: 0
    }
    background: Rectangle {
        radius: Theme.radiusSmall
        color: root.highlighted ? Theme.accentSoft : "transparent"
    }
}

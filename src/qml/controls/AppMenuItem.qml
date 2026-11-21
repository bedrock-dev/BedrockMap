import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

MenuItem {
    id: root
    property bool danger: false
    property string iconName

    readonly property real rowHeight: Theme.menuItemHeight
    readonly property bool hasIcon: root.iconName.length > 0
    readonly property bool hasSubMenu: root.subMenu !== null
    readonly property real iconSpace: hasIcon ? Theme.iconSmallSize + Theme.gapMedium : 0
    readonly property real subMenuSpace: hasSubMenu ? Theme.ui(14) : 0
    readonly property color iconColor: root.danger ? Theme.danger : root.highlighted ? Theme.accent : Theme.muted
    readonly property color labelColor: root.danger ? Theme.danger : Theme.text
    readonly property real contentWidth: contentItem ? contentItem.implicitWidth + Theme.gapLarge * 2 : Theme.ui(160)

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
    implicitWidth: visible ? Math.max(Theme.ui(160), root.contentWidth) : 0
    width: visible && parent ? parent.width : 0

    contentItem: Item {
        implicitWidth: Math.max(Theme.ui(160), label.implicitWidth + Theme.gapLarge * 2 + root.iconSpace
                                + root.subMenuSpace)
        implicitHeight: root.rowHeight
        AppIcon {
            visible: root.hasIcon
            width: Theme.iconSmallSize
            height: width
            anchors.left: parent.left
            anchors.leftMargin: Theme.gapLarge
            anchors.verticalCenter: parent.verticalCenter
            name: root.iconName
            iconSize: width
            iconColor: root.iconColor
        }
        Text {
            id: label
            anchors.left: parent.left
            anchors.leftMargin: Theme.gapLarge + root.iconSpace
            anchors.right: parent.right
            anchors.rightMargin: root.hasSubMenu ? Theme.iconSmallSize + Theme.gapMedium : Theme.gapLarge
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.labelColor
            font.pixelSize: Theme.fontMedium
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: Theme.gapLarge
            anchors.verticalCenter: parent.verticalCenter
            visible: root.hasSubMenu
            text: "›"
            color: Theme.muted
            font.pixelSize: Theme.fontMedium
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

import BedrockMap 1.0
import QtQuick

Rectangle {
    id: root
    property string icon
    property string iconName
    property bool dirty: false
    property string label
    property string detail
    property bool selected: false
    property bool interactive: true
    property bool compact: false
    property bool stacked: false
    signal clicked

    readonly property real labelHeight: Theme.fontSubtitle + (detail.length > 0 ? Theme.fontBody + Theme.gapTiny : 0)
    readonly property real rowHeight: Theme.ui(detail.length > 0 ? (Theme.compact ? 54 : 68) : (Theme.compact ? 42 :
                                                                                                                46))
    readonly property bool activeBackdrop: selected || ((mouse.pressed || mouse.containsMouse) && interactive)
    readonly property color activeBackdropColor: selected ? Theme.accentSoft : (activeBackdrop ? Theme.surfaceAlt :
                                                                                              "transparent")

    implicitWidth: parent ? parent.width : 0
    implicitHeight: stacked ? Math.max(Theme.ui(Theme.compact ? 64 : 72), Theme.iconLargeSize + Theme.fontSubtitle
                                       + Theme.gapSmall + Theme.gapLarge * 2) : Math.max(rowHeight, Theme.touch
                                                                                            ? Math.max(
                                                                                                  Theme.controlHeight,
                                                                                                  labelHeight
                                                                                                  + Theme.gapLarge
                                                                                                  * 2) : 0)
    height: implicitHeight
    radius: Theme.radiusCard
    color: compact ? "transparent" : activeBackdropColor
    border.width: 0
    opacity: enabled ? 1.0 : 0.5

    Rectangle {
        visible: root.compact
        width: Math.min(root.width, root.height)
        height: width
        anchors.centerIn: parent
        radius: Theme.radiusCard
        color: root.activeBackdropColor
    }

    Row {
        visible: !root.stacked
        x: root.compact ? (parent.width - width) / 2 : Theme.gapLarge
        width: root.compact ? implicitWidth : parent.width - x * 2
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.gapLarge
        Item {
            width: Theme.ui(20)
            height: width
            anchors.verticalCenter: parent.verticalCenter
            AppIcon {
                anchors.fill: parent
                name: root.iconName
                fallback: root.icon
                iconSize: width
                iconColor: root.selected ? Theme.accent : Theme.muted
            }
            AppDirtyIndicator {
                id: dirtyIndicator
                dirty: root.dirty
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: -Theme.gapTiny
            }
        }
        Column {
            visible: !root.compact
            width: Math.max(0, parent.width - Theme.ui(20) - parent.spacing - Theme.iconSize)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.gapTiny
            Text {
                width: parent.width
                text: root.label
                color: root.selected ? Theme.accent : Theme.text
                font.pixelSize: Theme.fontSubtitle
                elide: Text.ElideRight
            }
            Text {
                visible: root.detail.length > 0
                width: parent.width
                text: root.detail
                color: Theme.muted
                font.pixelSize: Theme.fontBody
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }
    }

    Column {
        visible: root.stacked
        anchors.centerIn: parent
        spacing: Theme.gapSmall
        AppIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.iconLargeSize
            height: width
            name: root.iconName
            fallback: root.icon
            iconSize: width
            iconColor: root.selected ? Theme.accent : Theme.muted
            AppDirtyIndicator {
                dirty: root.dirty
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: -Theme.gapTiny
            }
        }
        Text {
            width: Math.max(0, root.width - Theme.gapLarge * 2)
            text: root.label
            color: root.selected ? Theme.accent : Theme.text
            font.pixelSize: Theme.fontSubtitle
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }

    Text {
        visible: root.detail.length > 0 && root.interactive && !root.compact && !root.stacked
        anchors.right: parent.right
        anchors.rightMargin: Theme.gapLarge
        anchors.verticalCenter: parent.verticalCenter
        text: "›"
        color: Theme.accent
        font.pixelSize: Theme.fontHeading
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: root.interactive
        hoverEnabled: root.interactive
        cursorShape: root.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.clicked()
    }

    AppToolTip {
        text: root.label + (root.dirty ? "\n" + dirtyIndicator.description : "")
        shown: (root.compact || root.dirty) && mouse.containsMouse
    }
    Accessible.description: root.dirty ? dirtyIndicator.description : ""
}

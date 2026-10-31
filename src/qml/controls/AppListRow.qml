import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

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

    readonly property real labelHeight: Theme.fontSubtitle + (detail.length > 0 ? Theme.fontBody + Theme.ui(2) : 0)
    readonly property real rowHeight: Theme.ui(detail.length > 0 ? (Theme.compact ? 54 : 68) : (Theme.compact ? 42 :
                                                                                                                46))

    width: parent.width
    implicitHeight: stacked ? Math.max(Theme.ui(Theme.compact ? 64 : 72), Theme.iconLargeSize + Theme.fontSubtitle
                                       + Theme.gapSmall + Theme.cardPadding * 2) : Math.max(rowHeight, Theme.touch
                                                                                            ? Math.max(
                                                                                                  Theme.controlHeight,
                                                                                                  labelHeight
                                                                                                  + Theme.cardPadding
                                                                                                  * 2) : 0)
    height: implicitHeight
    radius: Theme.radiusCard
    color: selected ? Theme.accentSoft : ((mouse.pressed || mouse.containsMouse) && interactive ? Theme.surfaceAlt :
                                                                                                  "transparent")
    border.width: 0
    opacity: enabled ? 1.0 : 0.5

    Row {
        visible: !root.stacked
        x: root.compact ? (parent.width - width) / 2 : Theme.ui(Theme.compact ? 8 : 13)
        width: root.compact ? implicitWidth : parent.width - x * 2
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.ui(Theme.compact ? 8 : 13)
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
                anchors.margins: -2
            }
        }
        Column {
            visible: !root.compact
            width: Math.max(0, parent.width - Theme.ui(20) - parent.spacing - Theme.iconSize)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.ui(2)
            Text {
                width: parent.width
                text: root.label
                color: root.selected ? Theme.accent : Theme.text
                font.pixelSize: root.detail.length > 0 ? Theme.fontSubtitle : Theme.fontSubtitle
                font.bold: root.selected || root.detail.length > 0
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
                anchors.margins: -2
            }
        }
        Text {
            width: Math.max(0, root.width - Theme.cardPadding * 2)
            text: root.label
            color: root.selected ? Theme.accent : Theme.text
            font.pixelSize: Theme.fontSubtitle
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }

    Text {
        visible: root.detail.length > 0 && root.interactive && !root.compact && !root.stacked
        anchors.right: parent.right
        anchors.rightMargin: Theme.cardPadding
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

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

AbstractButton {
    id: root
    required property int sessionIndex
    required property string title
    required property bool dirty
    required property string kind
    property string path: ""
    property bool active: false
    property bool railMode: false
    property bool pageMode: false
    property bool showPath: true
    property real preferredHeight: Theme.sessionRowHeight

    readonly property string iconName: SessionKindRegistry.iconName(kind)
    readonly property string displayTitle: title || SessionKindRegistry.untitledTitle(kind)
    readonly property real iconBadgeSize: railMode ? Theme.ui(20) : Theme.ui(34)
    readonly property real iconSize: railMode ? Theme.ui(14) : Theme.iconSize

    signal closeRequested

    width: parent ? parent.width : 0
    height: preferredHeight
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    onClicked: App.selectWorkspace(sessionIndex)

    Keys.onReturnPressed: animateClick()
    Keys.onEnterPressed: animateClick()

    background: Rectangle {
        radius: root.pageMode ? Theme.radiusLarge : Theme.radiusMedium
        color: root.active ? Theme.accentSoft : (root.hovered || root.pressed ? Theme.surfaceAlt : (root.pageMode
                                                                                                    ? Theme.surface :
                                                                                                      "transparent"))
        border.width: root.visualFocus ? Theme.borderWidth * 3 : (root.active || root.pageMode ? Theme.borderWidth : 0)
        border.color: root.visualFocus || root.active ? Theme.accent : Theme.border
    }

    contentItem: Item {
        Row {
            x: root.railMode ? (parent.width - width) / 2 : Theme.gapLarge
            anchors.verticalCenter: parent.verticalCenter
            spacing: root.railMode ? 0 : Theme.gapMedium

            Rectangle {
                width: root.iconBadgeSize
                height: width
                radius: root.railMode ? width / 2 : Theme.radiusMedium
                anchors.verticalCenter: parent.verticalCenter
                color: root.active ? Theme.accentSoft : "transparent"

                AppIcon {
                    anchors.centerIn: parent
                    width: root.iconSize
                    height: width
                    name: root.iconName
                    iconSize: width
                    iconColor: root.active ? Theme.accent : Theme.muted
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
                visible: !root.railMode
                width: Math.max(0, parent.parent.width - Theme.gapLarge * 2 - root.iconBadgeSize - parent.spacing
                                - closeButton.width)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.gapTiny
                Text {
                    width: parent.width
                    text: root.displayTitle
                    color: root.active ? Theme.accent : Theme.text
                    font.pixelSize: Theme.fontMedium
                    elide: Text.ElideRight
                }
                Text {
                    visible: root.showPath && root.path.length > 0
                    width: parent.width
                    text: root.path
                    color: Theme.muted
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideMiddle
                }
            }
        }

        AppIconButton {
            id: closeButton
            visible: !root.railMode
            anchors.right: parent.right
            anchors.rightMargin: Theme.gapSmall
            anchors.verticalCenter: parent.verticalCenter
            buttonSize: Theme.iconButtonSmallSize
            iconName: "close"
            label: qsTr("Close session")
            danger: true
            onClicked: root.closeRequested()
        }
    }

    AppToolTip {
        text: root.displayTitle + (root.dirty ? "\n" + dirtyIndicator.description : "")
        shown: root.railMode && root.hovered
    }

    Accessible.name: root.displayTitle
    Accessible.description: root.dirty ? dirtyIndicator.description : root.path
}

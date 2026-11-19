pragma ComponentBehavior: Bound

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root

    property var items: []
    property real topOffset: 0
    readonly property real windowMargin: Theme.gapMedium
    readonly property real itemHeight: Theme.ui(48)
    readonly property real itemSpacing: Theme.gapTiny

    signal itemTriggered(var item)

    parent: Overlay.overlay
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: Theme.gapMedium
    width: Math.max(0, Math.min(Theme.ui(360), parent ? parent.width - windowMargin * 2 : Theme.ui(360)))
    height: navigationGrid.implicitHeight + topPadding + bottomPadding
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: topOffset

    background: AppCard {}

    component NavigationItem: AbstractButton {
        id: item
        required property var modelData

        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 0
        Layout.preferredHeight: root.itemHeight
        padding: 0
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        enabled: item.modelData.enabled !== false
        opacity: enabled ? 1.0 : 0.45

        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()

        background: Rectangle {
            radius: Theme.radiusControl
            color: {
                if (item.modelData.selected)
                return Theme.accentSoft
                return item.hovered || item.pressed ? Theme.surfaceAlt : "transparent"
            }

            border.width: item.modelData.selected || item.visualFocus ? Theme.borderWidth : 0
            border.color: Theme.accent
        }

        contentItem: Item {
            Column {
                anchors.centerIn: parent
                width: parent.width
                spacing: Theme.gapTiny

                Item {
                    width: parent.width
                    height: Theme.iconSize

                    AppIcon {
                        anchors.centerIn: parent
                        width: Theme.iconSize
                        height: width
                        name: item.modelData.iconName
                        fallback: item.modelData.iconText || ""
                        iconSize: width
                        iconColor: item.modelData.selected ? Theme.accent : Theme.text
                    }

                    AppDirtyIndicator {
                        dirty: !!item.modelData.dirty
                        anchors.top: parent.verticalCenter
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.horizontalCenterOffset: Theme.iconSize * 0.42
                        anchors.topMargin: -Theme.iconSize * 0.35
                    }
                }

                Text {
                    width: parent.width
                    text: item.modelData.label
                    color: item.modelData.selected ? Theme.accent : Theme.text
                    font.pixelSize: Theme.fontCaption
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }
        }

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        onClicked: {
            root.itemTriggered(item.modelData)
            root.close()
        }
    }

    contentItem: GridLayout {
        id: navigationGrid
        columns: 4
        columnSpacing: root.itemSpacing
        rowSpacing: root.itemSpacing

        Repeater {
            model: root.items
            delegate: NavigationItem {}
        }
    }
}

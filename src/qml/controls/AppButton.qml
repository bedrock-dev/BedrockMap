import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

AbstractButton {
    id: root
    property string iconText
    property string iconName
    property string tooltip
    property bool primary: false
    property bool danger: false
    property bool active: false
    property bool compact: false
    property bool ghost: false
    property bool bordered: true
    property real visualHeight: Theme.touch ? Theme.controlVisualHeight : height

    readonly property bool hot: hovered || pressed
    readonly property color contentColor: primary ? Theme.onAccent : (danger ? Theme.danger : (active ? Theme.accent :
                                                                                                     Theme.text))
    implicitWidth: Math.max(Theme.ui(compact ? 60 : 82), label.implicitWidth + Theme.gapLarge * 2 + (
                                buttonIcon.visible ? buttonIcon.width + Theme.gapMedium : 0))
    implicitHeight: compact ? Theme.compactButtonHeight : Theme.buttonHeight
    padding: 0
    hoverEnabled: true
    // Tab reaches the button without a click stealing focus from the map or editors.
    focusPolicy: Qt.TabFocus
    opacity: enabled ? 1.0 : 0.5

    Keys.onReturnPressed: animateClick()
    Keys.onEnterPressed: animateClick()

    background: Item {
        Rectangle {
            anchors.centerIn: parent
            width: parent.width
            height: Math.max(0, Math.min(root.height, root.visualHeight))
            radius: Theme.radiusControl
            color: root.primary ? Theme.accent : root.danger ? Theme.dangerSoft : root.active ? Theme.accentSoft :
                                                                                                root.ghost ? (root.hot ?
                                                                                                                  Theme.surfaceAlt :
                                                                                                                  "transparent") :
                                                                                                             (root.hot ?
                                                                                                                  Theme.surface :
                                                                                                                  Theme.surfaceAlt)
            border.width: root.visualFocus ? Theme.borderWidth * 3 : (root.bordered && !(root.ghost && !root.active
                                                                                          && !root.hot) ?
                                                                          Theme.borderWidth : 0)
            border.color: root.visualFocus || root.primary ? Theme.accent : root.danger ? Theme.danger : (root.active
                                                                                                          || root.hot) ?
                                                                                                             Theme.accent :
                                                                                                             Theme.border
        }
    }

    contentItem: Item {
        Item {
            anchors.centerIn: parent
            width: Math.max(0, Math.min(root.width - Theme.gapLarge * 2, label.implicitWidth + (buttonIcon.visible
                                                                                                      ? buttonIcon.width
                                                                                                        + Theme.gapMedium : 0)))
            height: parent.height
            AppIcon {
                id: buttonIcon
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                visible: root.iconName.length > 0 || root.iconText.length > 0
                width: Theme.iconSmallSize
                height: width
                name: root.iconName
                fallback: root.iconText
                iconSize: width
                iconColor: root.contentColor
            }
            Text {
                id: label
                anchors.left: buttonIcon.visible ? buttonIcon.right : parent.left
                anchors.leftMargin: buttonIcon.visible ? Theme.gapMedium : 0
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.text
                color: root.contentColor
                font.pixelSize: root.compact ? Theme.fontCaption : Theme.fontSubtitle
                elide: Text.ElideRight
            }
        }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    AppToolTip {
        text: root.tooltip
        shown: root.tooltip.length > 0 && root.hovered
    }
}

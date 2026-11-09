import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

Item {
    id: root
    property string text
    property string icon
    property string iconName
    property string tooltip
    property bool primary: false
    property bool danger: false
    property bool active: false
    property bool compact: false
    property bool ghost: false
    property bool bordered: true
    property real visualHeight: Theme.touch ? Theme.controlVisualHeight : height
    signal clicked

    readonly property bool hovered: mouse.containsMouse || mouse.pressed
    readonly property color contentColor: primary ? (Theme.dark ? "#000000" : "#ffffff") : (danger ? Theme.danger : (
                                                                                                         active ? Theme.accent :
                                                                                                                  Theme.text))
    implicitWidth: Math.max(Theme.ui(compact ? 60 : 82), label.implicitWidth + Theme.controlPadding * 2 + (
                                buttonIcon.visible ? buttonIcon.width + Theme.ui(7) : 0))
    implicitHeight: compact ? Theme.compactButtonHeight : Theme.buttonHeight
    opacity: enabled ? 1.0 : 0.5

    Rectangle {
        anchors.centerIn: parent
        width: parent.width
        height: Math.max(0, Math.min(root.height, root.visualHeight))
        radius: Theme.radiusControl
        color: root.primary ? Theme.accent : root.danger ? Theme.dangerSoft : root.active ? Theme.accentSoft :
                                                                                            root.ghost ? (root.hovered
                                                                                                          ? Theme.surfaceAlt :
                                                                                                            "transparent") :
                                                                                                         (root.hovered
                                                                                                          ? Theme.surface :
                                                                                                            Theme.surfaceAlt)
        border.width: root.bordered && !(root.ghost && !root.active && !root.hovered) ? Theme.borderWidth : 0
        border.color: root.primary ? Theme.accent : root.danger ? Theme.danger : (root.active || root.hovered)
                                                                  ? Theme.accent : Theme.border
    }

    Item {
        anchors.centerIn: parent
        width: Math.max(0, Math.min(root.width - Theme.controlPadding * 2, label.implicitWidth + (buttonIcon.visible
                                                                                                  ? buttonIcon.width
                                                                                                    + Theme.ui(7) : 0)))
        height: parent.height
        AppIcon {
            id: buttonIcon
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            visible: root.iconName.length > 0 || root.icon.length > 0
            width: Theme.iconSmallSize
            height: width
            name: root.iconName
            fallback: root.icon
            iconSize: width
            iconColor: root.contentColor
        }
        Text {
            id: label
            anchors.left: buttonIcon.visible ? buttonIcon.right : parent.left
            anchors.leftMargin: buttonIcon.visible ? Theme.ui(7) : 0
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.contentColor
            font.pixelSize: root.compact ? Theme.fontCaption : Theme.fontSubtitle
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
    Accessible.name: text
    AppToolTip {
        text: root.tooltip
        shown: root.tooltip.length > 0 && mouse.containsMouse
    }
}

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

Rectangle {
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
    signal clicked

    readonly property bool hovered: mouse.containsMouse || mouse.pressed
    readonly property color contentColor: primary ? (Theme.dark ? "#000000" : "#ffffff") : (
                                                       danger ? Theme.danger : (active ? Theme.accent : Theme.text))
    implicitWidth: Math.max(compact ? 60 : 82, label.implicitWidth + 20
                                                + (iconName.length > 0 ? 23 : (icon.length > 0 ? 14 : 0)))
    implicitHeight: Theme.touch ? Theme.touchTarget : (compact ? 28 : 32)
    radius: Theme.radiusControl
    color: primary ? Theme.accent : danger ? Theme.dangerSoft : active ? Theme.accentSoft : ghost ? (
                                                                                                        hovered
                                                                                                        ? Theme.surfaceAlt :
                                                                                                          "transparent") :
                                                                                                    (hovered
                                                                                                     ? Theme.surface :
                                                                                                       Theme.surfaceAlt)
    border.width: bordered && !(ghost && !active && !hovered) ? 1 : 0
    border.color: primary ? Theme.accent : danger ? Theme.danger : (active || hovered) ? Theme.accent :
                                                                                         Theme.border
    opacity: enabled ? 1.0 : 0.5

    Row {
        anchors.centerIn: parent
        spacing: (root.icon.length > 0 || root.iconName.length > 0) && root.text.length > 0 ? 7 : 0
        AppIcon {
            visible: root.iconName.length > 0
            width: 16
            height: 16
            name: root.iconName
            iconSize: width
            iconColor: root.contentColor
        }
        Text {
            visible: root.iconName.length === 0 && root.icon.length > 0
            text: root.icon
            color: root.contentColor
            font.pixelSize: Theme.fontBody
        }
        Text {
            id: label
            text: root.text
            color: root.contentColor
            font.pixelSize: root.compact ? Theme.fontCaption : Theme.fontSubtitle
            font.bold: root.active
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

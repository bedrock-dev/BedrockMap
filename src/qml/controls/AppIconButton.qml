import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

Item {
    id: root
    property string icon
    property string iconName
    property string label
    property bool selected: false
    property bool dirty: false
    property bool danger: false
    property int buttonSize: 36
    signal clicked

    readonly property real controlSize: Math.max(buttonSize, Theme.touch ? Theme.controlHeight : 0)
    // Keep the visual icon independent from the larger touch target.
    readonly property real iconSize: Theme.touch ? Theme.iconSize : Math.min(20, Math.max(15, root.buttonSize - 16))
    readonly property bool hovered: mouse.containsMouse || mouse.pressed

    width: controlSize
    height: controlSize
    implicitWidth: controlSize
    implicitHeight: controlSize
    opacity: enabled ? 1.0 : 0.5

    Rectangle {
        anchors.centerIn: parent
        width: Theme.touch ? Math.min(root.width, Theme.controlVisualHeight) : root.width
        height: Theme.touch ? Math.min(root.height, Theme.controlVisualHeight) : root.height
        radius: Theme.radiusControl
        color: root.selected ? Theme.accentSoft : (root.hovered ? (root.danger ? Theme.dangerSoft : Theme.surfaceAlt) :
                                                                  "transparent")
        border.width: root.selected ? 1 : 0
        border.color: Theme.accent
    }

    AppIcon {
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        name: root.iconName
        fallback: root.icon
        iconSize: root.iconSize
        iconColor: root.danger ? Theme.danger : (root.selected ? Theme.accent : (root.buttonSize <= 24 ? Theme.muted :
                                                                                                         Theme.text))
        AppDirtyIndicator {
            id: dirtyIndicator
            dirty: root.dirty
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: -2
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
    Accessible.name: label
    Accessible.description: root.dirty ? dirtyIndicator.description : ""
    AppToolTip {
        text: root.label + (root.dirty ? "\n" + dirtyIndicator.description : "")
        shown: root.label.length > 0 && mouse.containsMouse
    }
}

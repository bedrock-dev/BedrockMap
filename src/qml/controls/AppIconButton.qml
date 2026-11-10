import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

AbstractButton {
    id: root
    property string iconText
    property string iconName
    property string label
    property bool selected: false
    property bool dirty: false
    property bool danger: false
    property int buttonSize: Theme.iconButtonSize

    readonly property real controlSize: Math.max(buttonSize, Theme.touch ? Theme.controlHeight : 0)
    // Keep the visual icon independent from the larger touch target.
    readonly property real iconSize: Theme.touch ? Theme.iconSize : Math.min(20, Math.max(15, root.buttonSize - 16))
    readonly property bool hot: hovered || pressed

    width: controlSize
    height: controlSize
    implicitWidth: controlSize
    implicitHeight: controlSize
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
            width: Theme.touch ? Math.min(root.width, Theme.controlVisualHeight) : root.width
            height: Theme.touch ? Math.min(root.height, Theme.controlVisualHeight) : root.height
            radius: Theme.radiusControl
            color: root.selected ? Theme.accentSoft : (root.hot ? (root.danger ? Theme.dangerSoft : Theme.surfaceAlt) :
                                                                  "transparent")
            border.width: root.visualFocus ? Theme.borderWidth * 3 : (root.selected ? Theme.borderWidth : 0)
            border.color: Theme.accent
        }
    }

    contentItem: Item {
        AppIcon {
            anchors.centerIn: parent
            width: root.iconSize
            height: root.iconSize
            name: root.iconName
            fallback: root.iconText
            iconSize: root.iconSize
            iconColor: root.danger ? Theme.danger : (root.selected ? Theme.accent : Theme.text)
            AppDirtyIndicator {
                id: dirtyIndicator
                dirty: root.dirty
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: -Theme.gapTiny
            }
        }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    Accessible.name: label
    Accessible.description: root.dirty ? dirtyIndicator.description : ""
    AppToolTip {
        text: root.label + (root.dirty ? "\n" + dirtyIndicator.description : "")
        shown: root.label.length > 0 && root.hovered
    }
}

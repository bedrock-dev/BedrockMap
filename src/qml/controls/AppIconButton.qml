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
    readonly property real visualWidth: Theme.touch ? Math.min(root.width, Theme.controlVisualHeight) : root.width
    readonly property real visualHeight: Theme.touch ? Math.min(root.height, Theme.controlVisualHeight) : root.height
    readonly property color backgroundColor: {
        if (root.selected)
            return Theme.accentSoft
        if (!root.hot)
            return "transparent"
        return root.danger ? Theme.dangerSoft : Theme.surfaceAlt
    }
    readonly property real visualBorderWidth: {
        if (root.visualFocus)
            return Theme.borderWidth * 3
        return root.selected ? Theme.borderWidth : 0
    }
    readonly property color iconColor: {
        if (root.danger)
            return Theme.danger
        return root.selected ? Theme.accent : Theme.text
    }

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
            width: root.visualWidth
            height: root.visualHeight
            radius: Theme.radiusControl
            color: root.backgroundColor
            border.width: root.visualBorderWidth
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
            iconColor: root.iconColor
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

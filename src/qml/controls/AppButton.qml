import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

AbstractButton {
    id: root
    property string tooltip
    property bool primary: false
    property bool danger: false
    property bool active: false
    property bool compact: false
    property bool flat: false
    property bool showBorder: true
    property real visualHeight: Theme.touch ? Theme.controlVisualHeight : height
    property real cornerRadius: Theme.radiusMedium

    readonly property bool hot: hovered || pressed
    readonly property color contentColor: {
        if (root.primary)
            return "#ffffff"
        if (root.danger)
            return Theme.danger
        if (root.active)
            return Theme.accent
        return Theme.text
    }
    readonly property color backgroundColor: {
        if (root.primary)
            return Theme.accent
        if (root.danger)
            return Theme.dangerSoft
        if (root.active)
            return Theme.accentSoft
        if (root.flat)
            return root.hot ? Theme.surfaceAlt : "transparent"
        return root.hot ? Theme.surface : Theme.surfaceAlt
    }
    readonly property real visualBorderWidth: {
        if (!root.showBorder)
            return 0
        if (root.visualFocus)
            return Theme.borderWidth * 3
        return Theme.borderWidth
    }
    readonly property color visualBorderColor: {
        if (root.visualFocus || root.primary)
            return Theme.accent
        if (root.danger)
            return Theme.danger
        return Theme.border
    }
    implicitWidth: Math.max(Theme.ui(compact ? 60 : 82), label.implicitWidth + Theme.gapLarge * 2)
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
            radius: root.cornerRadius
            color: root.backgroundColor
            border.width: root.visualBorderWidth
            border.color: root.visualBorderColor
        }
    }

    contentItem: Item {
        Item {
            anchors.centerIn: parent
            width: Math.max(0, Math.min(root.width - Theme.gapLarge * 2, label.implicitWidth))
            height: parent.height
            Text {
                id: label
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.text
                color: root.contentColor
                font.pixelSize: root.compact ? Theme.fontSmall : Theme.fontMedium
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

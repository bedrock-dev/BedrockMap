import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

AbstractButton {
    id: root
    property bool compact: false

    readonly property bool hot: hovered || pressed
    readonly property real horizontalTextPadding: Theme.touch ? Theme.gapLarge * 2 : (root.compact ? 14 : 20)
    readonly property real visualHeight: Theme.touch ? Math.min(root.height, Theme.controlVisualHeight) : root.height
    readonly property color backgroundColor: {
        if (root.checked || root.pressed)
            return Theme.accentSoft
        return root.hovered ? Theme.surface : Theme.surfaceAlt
    }
    readonly property color labelColor: {
        if (root.checked)
            return Theme.accent
        return root.hot ? Theme.text : Theme.muted
    }

    implicitWidth: Math.max(Theme.ui(compact ? 54 : 68), Math.ceil(labelMetrics.advanceWidth)
                            + root.horizontalTextPadding)
    implicitHeight: Theme.touch ? Theme.controlHeight : (compact ? 24 : Theme.controlHeight)
    width: implicitWidth
    height: implicitHeight
    padding: 0
    checkable: true
    hoverEnabled: true
    // Tab reaches the toggle without a click stealing focus from the map or editors.
    focusPolicy: Qt.TabFocus
    opacity: enabled ? 1.0 : 0.5

    Keys.onReturnPressed: animateClick()
    Keys.onEnterPressed: animateClick()

    background: Item {
        Rectangle {
            anchors.centerIn: parent
            width: parent.width
            height: root.visualHeight
            radius: Theme.radiusMedium
            color: root.backgroundColor
            border.width: root.visualFocus ? Theme.borderWidth * 3 : Theme.borderWidth
            border.color: root.checked || root.visualFocus ? Theme.accent : Theme.border
        }
    }

    contentItem: Item {
        Text {
            id: labelText
            anchors.centerIn: parent
            width: Math.max(0, root.width - root.horizontalTextPadding)
            text: root.text
            color: root.labelColor
            font.pixelSize: root.compact ? Theme.fontSmall : Theme.fontMedium
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }

    TextMetrics {
        id: labelMetrics
        font.pixelSize: root.compact ? Theme.fontSmall : Theme.fontMedium
        text: root.text
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }
}

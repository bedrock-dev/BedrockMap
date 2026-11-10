import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

AbstractButton {
    id: root
    property bool compact: false

    readonly property bool hot: hovered || pressed

    implicitWidth: Math.max(Theme.ui(compact ? 54 : 68), labelText.implicitWidth + (Theme.touch ? Theme.gapLarge
                                                                                                  * 2 : (compact ? 14 :
                                                                                                                   20)))
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
            height: Theme.touch ? Math.min(root.height, Theme.controlVisualHeight) : root.height
            radius: Theme.radiusControl
            color: root.checked || root.pressed ? Theme.accentSoft : (root.hovered ? Theme.surface : Theme.surfaceAlt)
            border.width: root.visualFocus ? Theme.borderWidth * 3 : Theme.borderWidth
            border.color: root.checked || root.hot || root.visualFocus ? Theme.accent : Theme.border
        }
    }

    contentItem: Item {
        Text {
            id: labelText
            anchors.centerIn: parent
            width: Math.max(0, root.width - Theme.gapLarge * 2)
            text: root.text
            color: root.checked ? Theme.accent : (root.hot ? Theme.text : Theme.muted)
            font.pixelSize: root.compact ? Theme.fontCaption : Theme.fontBody
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }
}

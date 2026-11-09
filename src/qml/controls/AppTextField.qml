import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

TextField {
    id: root

    implicitWidth: Theme.ui(170)
    implicitHeight: Theme.controlHeight
    font.pixelSize: Theme.fontSubtitle
    color: Theme.text
    placeholderTextColor: Theme.muted
    selectionColor: Theme.accent
    selectedTextColor: Theme.onAccent
    background: Rectangle {
        y: Theme.touch ? Math.max(0, (root.height - Theme.controlVisualHeight) / 2) : 0
        height: root.height - y * 2
        radius: Theme.radiusSmall
        color: Theme.surfaceAlt
        border.width: Theme.borderWidth
        border.color: root.activeFocus ? Theme.accent : Theme.border
        opacity: root.enabled ? 1.0 : 0.5
    }
}

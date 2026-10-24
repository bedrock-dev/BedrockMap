import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

TextField {
    id: root

    implicitWidth: 170
    implicitHeight: Theme.controlHeight
    font.pixelSize: Theme.fontSubtitle
    color: Theme.text
    placeholderTextColor: Theme.muted
    selectionColor: Theme.accent
    selectedTextColor: Theme.onAccent
    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surfaceAlt
        border.width: 1
        border.color: root.activeFocus ? Theme.accent : Theme.border
        opacity: root.enabled ? 1.0 : 0.5
    }
}

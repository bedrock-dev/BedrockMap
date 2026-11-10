import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

TextArea {
    id: root

    implicitWidth: Theme.ui(170)
    implicitHeight: Math.max(Theme.ui(72), Theme.controlHeight)
    font.pixelSize: Theme.fontSubtitle
    color: Theme.text
    placeholderTextColor: Theme.muted
    selectionColor: Theme.accent
    selectedTextColor: Theme.onAccent
    wrapMode: TextEdit.Wrap
    selectByMouse: true
    background: Rectangle {
        radius: Theme.radiusControl
        color: Theme.surfaceAlt
        border.width: Theme.borderWidth
        border.color: root.activeFocus ? Theme.accent : Theme.border
        opacity: root.enabled ? 1.0 : 0.5
    }
}

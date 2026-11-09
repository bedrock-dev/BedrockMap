import BedrockMap 1.0
import QtQuick

Rectangle {
    id: root
    property bool interactive: false
    property bool selected: false
    property bool hovered: false
    property bool danger: false

    radius: Theme.radiusCard
    color: selected ? Theme.accentSoft : (interactive && hovered ? Theme.surfaceAlt : Theme.surface)
    border.width: Theme.borderWidth
    border.color: danger ? Theme.danger : (selected || (interactive && hovered) ? Theme.accent : Theme.border)
    opacity: enabled ? 1.0 : 0.5
}

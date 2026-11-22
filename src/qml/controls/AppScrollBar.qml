import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

ScrollBar {
    id: root

    policy: ScrollBar.AsNeeded
    contentItem: Rectangle {
        implicitWidth: Theme.ui(6)
        implicitHeight: Theme.ui(6)
        radius: Math.min(width, height) / 2
        color: Theme.border
        opacity: root.pressed || root.hovered ? 0.95 : 0.55
    }
    background: Rectangle {
        color: "transparent"
    }
}

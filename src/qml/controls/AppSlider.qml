import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

Slider {
    id: root

    implicitHeight: Theme.touch ? Theme.touchTarget : Theme.controlHeight
    background: Rectangle {
        x: root.leftPadding
        y: root.topPadding + root.availableHeight / 2 - height / 2
        implicitWidth: 200
        implicitHeight: 4
        width: root.availableWidth
        height: 4
        radius: height / 2
        color: Theme.surfaceAlt
        Rectangle {
            width: root.visualPosition * parent.width
            height: parent.height
            radius: height / 2
            color: Theme.accent
        }
    }
    handle: Rectangle {
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + root.availableHeight / 2 - height / 2
        implicitWidth: Theme.touch ? 20 : 14
        implicitHeight: Theme.touch ? 20 : 14
        radius: height / 2
        color: root.enabled ? Theme.accent : Theme.muted
        border.width: 1
        border.color: Theme.surface
    }
}

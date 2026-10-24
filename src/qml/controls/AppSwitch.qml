import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

Switch {
    id: root
    property string tooltip

    hoverEnabled: true
    implicitHeight: Theme.touch ? Theme.touchTarget : Theme.controlHeight
    indicator: Rectangle {
        implicitWidth: Theme.touch ? 48 : 40
        implicitHeight: Theme.touch ? 28 : 22
        x: root.leftPadding
        y: parent.height / 2 - height / 2
        radius: height / 2
        color: root.checked ? Theme.accentSoft : Theme.surfaceAlt
        border.width: 1
        border.color: root.checked ? Theme.accent : Theme.border
        opacity: root.enabled ? 1.0 : 0.5
        Rectangle {
            x: root.checked ? parent.width - width - 3 : 3
            y: 3
            width: Theme.touch ? 22 : 16
            height: Theme.touch ? 22 : 16
            radius: height / 2
            color: root.checked ? Theme.accent : Theme.muted
            Behavior on x {
                NumberAnimation {
                    duration: 120
                }
            }
        }
    }

    AppToolTip {
        text: root.tooltip
        shown: root.tooltip.length > 0 && root.hovered
    }
}

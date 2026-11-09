import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

Switch {
    id: root
    property string tooltip

    hoverEnabled: true
    implicitHeight: Theme.controlHeight
    indicator: Rectangle {
        implicitWidth: Theme.switchWidth
        implicitHeight: Theme.switchHeight
        x: root.leftPadding
        y: parent.height / 2 - height / 2
        radius: height / 2
        color: root.checked ? Theme.accentSoft : Theme.surfaceAlt
        border.width: Theme.borderWidth
        border.color: root.checked ? Theme.accent : Theme.border
        opacity: root.enabled ? 1.0 : 0.5
        Rectangle {
            x: root.checked ? parent.width - width - Theme.switchInset : Theme.switchInset
            y: Theme.switchInset
            width: Theme.switchThumbSize
            height: width
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

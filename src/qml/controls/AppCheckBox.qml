import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

CheckBox {
    id: root

    implicitHeight: Math.max(Theme.controlHeight, implicitContentHeight + topPadding + bottomPadding)
    font.pixelSize: Theme.fontSubtitle
    spacing: Theme.controlSpacing
    contentItem: Text {
        leftPadding: root.indicator.width + root.spacing
        text: root.text
        color: root.enabled ? Theme.text : Theme.muted
        font: root.font
        verticalAlignment: Text.AlignVCenter
    }
    indicator: Rectangle {
        x: root.leftPadding
        y: parent.height / 2 - height / 2
        width: Theme.checkBoxSize
        height: width
        radius: Theme.radiusSmall
        color: root.checked ? Theme.accentSoft : Theme.surfaceAlt
        border.width: Theme.borderWidth
        border.color: root.checked ? Theme.accent : Theme.border
        Text {
            anchors.centerIn: parent
            visible: root.checked
            text: "✓"
            color: Theme.accent
            font.pixelSize: Theme.fontBody
            font.bold: true
        }
    }
}

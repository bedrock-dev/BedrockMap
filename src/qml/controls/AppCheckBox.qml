import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

CheckBox {
    id: root

    implicitHeight: Theme.controlHeight
    font.pixelSize: Theme.fontSubtitle
    spacing: 8
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
        width: Theme.touch ? 24 : 18
        height: Theme.touch ? 24 : 18
        radius: Theme.radiusSmall
        color: root.checked ? Theme.accentSoft : Theme.surfaceAlt
        border.width: 1
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

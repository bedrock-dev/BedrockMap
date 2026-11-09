import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

SpinBox {
    id: root

    readonly property real arrowWidth: Theme.spinBoxArrowWidth
    implicitWidth: Theme.ui(110)
    implicitHeight: Theme.controlHeight
    font.pixelSize: Theme.fontSubtitle
    editable: true
    contentItem: TextInput {
        text: root.textFromValue(root.value, root.locale)
        font: root.font
        color: Theme.text
        selectionColor: Theme.accent
        selectedTextColor: Theme.onAccent
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        readOnly: !root.editable
        validator: root.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        rightPadding: root.arrowWidth + Theme.ui(2)
    }
    up.indicator: Rectangle {
        x: root.width - root.arrowWidth
        y: 0
        width: root.arrowWidth
        height: root.height / 2
        radius: Theme.radiusSmall
        color: root.up.pressed ? Theme.surfaceAlt : "transparent"
        Text {
            anchors.centerIn: parent
            text: "+"
            color: root.enabled ? Theme.text : Theme.muted
            font.pixelSize: Theme.fontSubtitle
        }
    }
    down.indicator: Rectangle {
        x: root.width - root.arrowWidth
        y: root.height / 2
        width: root.arrowWidth
        height: root.height / 2
        radius: Theme.radiusSmall
        color: root.down.pressed ? Theme.surfaceAlt : "transparent"
        Text {
            anchors.centerIn: parent
            text: "\u2212"
            color: root.enabled ? Theme.text : Theme.muted
            font.pixelSize: Theme.fontSubtitle
        }
    }
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

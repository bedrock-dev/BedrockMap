import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

ComboBox {
    id: root

    implicitWidth: 190
    implicitHeight: Theme.controlHeight
    font.pixelSize: Theme.fontSubtitle
    contentItem: Text {
        leftPadding: 9
        rightPadding: 24
        text: root.displayText
        font: root.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        x: root.width - width - 9
        y: root.topPadding + (root.availableHeight - height) / 2
        text: "\u25be"
        color: Theme.muted
        font.pixelSize: Theme.fontBody
    }
    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surfaceAlt
        border.width: 1
        border.color: root.activeFocus ? Theme.accent : Theme.border
        opacity: root.enabled ? 1.0 : 0.5
    }
    delegate: ItemDelegate {
        id: option
        required property int index
        required property var modelData
        width: root.width
        implicitHeight: Theme.controlHeight
        highlighted: root.highlightedIndex === index
        text: root.textRole ? modelData[root.textRole] : modelData
        contentItem: Text {
            leftPadding: 9
            rightPadding: 9
            text: option.text
            color: Theme.text
            font.pixelSize: Theme.fontSubtitle
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: Theme.radiusSmall
            color: option.highlighted ? Theme.surfaceAlt : "transparent"
        }
    }
    popup: Popup {
        y: root.height + 4
        width: root.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 260)
        padding: 4
        background: Rectangle {
            radius: Theme.radiusControl
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}

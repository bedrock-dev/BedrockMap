import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

ComboBox {
    id: root

    implicitWidth: Theme.ui(190)
    implicitHeight: Theme.controlHeight
    font.pixelSize: Theme.fontSubtitle
    contentItem: Text {
        leftPadding: Theme.gapLarge
        rightPadding: Theme.gapLarge
        text: root.displayText
        font: root.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Item {
        implicitWidth: 0
        implicitHeight: 0
    }
    background: Rectangle {
        y: Theme.touch ? Math.max(0, (root.height - Theme.controlVisualHeight) / 2) : 0
        height: root.height - y * 2
        radius: Theme.radiusControl
        color: Theme.surfaceAlt
        border.width: Theme.borderWidth
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
            leftPadding: Theme.gapLarge
            rightPadding: Theme.gapLarge
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
        y: root.height + Theme.gapSmall
        width: root.width
        implicitHeight: Math.min(contentItem.implicitHeight + padding * 2, Theme.ui(260))
        padding: Theme.gapSmall
        background: Rectangle {
            radius: Theme.radiusControl
            color: Theme.surface
            border.width: Theme.borderWidth
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

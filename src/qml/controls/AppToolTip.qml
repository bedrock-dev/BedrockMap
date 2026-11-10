import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

ToolTip {
    id: root
    property bool shown: false

    delay: 350
    padding: Theme.gapMedium
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? -height - Theme.gapSmall : 0
    contentItem: Text {
        text: root.text
        color: Theme.text
        font.pixelSize: Theme.fontCaption
        wrapMode: Text.Wrap
    }
    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: Theme.border
    }

    onShownChanged: {
        if (shown)
            root.show(root.text)
        else
            root.hide()
    }
}

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

Dialog {
    id: root
    // Preferred size; the dialog shrinks to fit the window. A negative
    // preferredHeight sizes the dialog to its content.
    property real preferredWidth: Theme.ui(430)
    property real preferredHeight: -1
    readonly property real windowMargin: Theme.gapSection

    modal: true
    parent: Overlay.overlay
    closePolicy: Popup.CloseOnEscape
    width: Math.min(preferredWidth, parent ? parent.width - windowMargin * 2 : preferredWidth)
    height: Math.min(preferredHeight >= 0 ? preferredHeight : implicitHeight, parent ? parent.height - windowMargin * 2 :
                                                                                       implicitHeight)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: Theme.gapLarge

    background: Rectangle {
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: Theme.border
    }

    header: Item {
        visible: root.title.length > 0
        implicitHeight: visible ? Theme.controlHeight + Theme.gapSmall : 0
        Text {
            anchors.fill: parent
            anchors.leftMargin: root.leftPadding
            anchors.rightMargin: root.rightPadding
            verticalAlignment: Text.AlignVCenter
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.fontLabel
            elide: Text.ElideRight
        }
    }
}

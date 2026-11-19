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
    readonly property real maximumDialogWidth: parent ? parent.width - windowMargin * 2 : preferredWidth
    readonly property real maximumDialogHeight: parent ? parent.height - windowMargin * 2 : implicitHeight
    readonly property real requestedHeight: preferredHeight >= 0 ? preferredHeight : implicitHeight

    modal: true
    parent: Overlay.overlay
    closePolicy: Popup.CloseOnEscape
    width: Math.min(root.preferredWidth, root.maximumDialogWidth)
    height: Math.min(root.requestedHeight, root.maximumDialogHeight)
    x: parent ? Math.round((parent.width - root.width) / 2) : 0
    y: parent ? Math.round((parent.height - root.height) / 2) : 0
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

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

// Titled card used by each part of the voxel preview's control panel.
GroupBox {
    id: section
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.alignment: Qt.AlignTop
    padding: Theme.gapSmall
    topPadding: padding + implicitLabelHeight + spacing
    spacing: Theme.gapSmall
    label: Text {
        x: section.leftPadding
        y: section.padding
        text: section.title
        color: Theme.text
        font.pixelSize: Theme.fontLarge
    }
    background: AppCard {}
}

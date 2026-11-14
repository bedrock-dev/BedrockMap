import BedrockMap 1.0
import QtQuick
import QtQuick.Layouts
import "controls"

// Check box whose label wraps instead of eliding in narrow preview panels.
AppCheckBox {
    id: toggle
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    implicitHeight: Math.max(Theme.controlHeight, implicitContentHeight + topPadding + bottomPadding)
    contentItem: Text {
        leftPadding: toggle.indicator.width + toggle.spacing
        text: toggle.text
        color: toggle.enabled ? Theme.text : Theme.muted
        font: toggle.font
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
    }
}

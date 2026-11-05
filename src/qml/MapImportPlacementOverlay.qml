import QtQuick
import QtQuick.Layouts
import BedrockMap 1.0
import "controls"

Item {
    id: root

    property var regionImport
    property bool inputBlocked: false
    property real inputRightInset: 0
    readonly property bool active: !!regionImport && regionImport.active
    readonly property real reservedHeight: panel.visible ? panel.height + 16 : 0

    signal cancelRequested
    signal confirmRequested

    function confirmPlacement() {
        if (active)
            confirmRequested()
    }

    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: root.visible && root.active && !root.inputBlocked
        onActivated: root.confirmPlacement()
    }

    Shortcut {
        sequence: "Esc"
        enabled: root.visible && root.active && !root.inputBlocked
        onActivated: root.cancelRequested()
    }

    AppCard {
        id: panel
        visible: root.active
        enabled: !root.inputBlocked
        z: 10
        x: Math.max(8, (root.width - root.inputRightInset - width) / 2)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        width: Math.max(0, Math.min(260, root.width - root.inputRightInset - 16))
        height: panelColumn.implicitHeight + 12

        // Reserve this entire strip from map input, including unused panel space.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: function (event) {
                event.accepted = true
            }
        }
        MultiPointTouchArea {
            anchors.fill: parent
            mouseEnabled: false
        }

        ColumnLayout {
            id: panelColumn
            anchors.fill: parent
            anchors.margins: 6
            spacing: 4

            Text {
                Layout.fillWidth: true
                text: root.regionImport ? qsTr("%1 · %2 chunks · %3 × %4").arg(root.regionImport.sourceName).arg(
                                              root.regionImport.chunkCount).arg(root.regionImport.width).arg(
                                              root.regionImport.height) : ""
                color: Theme.text
                font.pixelSize: Theme.fontBody
                font.bold: true
                elide: Text.ElideMiddle
            }
            RowLayout {
                Layout.fillWidth: true
                AppButton {
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    compact: true
                    onClicked: root.cancelRequested()
                }
                AppButton {
                    Layout.fillWidth: true
                    text: qsTr("Import")
                    primary: true
                    compact: true
                    enabled: root.active
                    onClicked: root.confirmPlacement()
                }
            }
        }
    }
}

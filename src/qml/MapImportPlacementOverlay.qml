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
        width: Math.max(0, Math.min(440, root.width - root.inputRightInset - 16))
        height: panelColumn.implicitHeight + 16

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
            anchors.margins: 8
            spacing: 6

            Text {
                Layout.fillWidth: true
                text: root.regionImport ? root.regionImport.sourceName : ""
                elide: Text.ElideMiddle
                color: Theme.text
                font.pixelSize: Theme.fontSubtitle
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: root.regionImport ? qsTr("%1 chunks · %2 × %3").arg(root.regionImport.chunkCount).arg(
                                              root.regionImport.width).arg(root.regionImport.height) : ""
                wrapMode: Text.Wrap
                color: Theme.muted
                font.pixelSize: Theme.fontCaption
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                AppButton {
                    text: qsTr("Cancel")
                    onClicked: root.cancelRequested()
                }
                AppButton {
                    text: qsTr("Import")
                    primary: true
                    enabled: root.active
                    onClicked: root.confirmPlacement()
                }
            }
        }
    }
}

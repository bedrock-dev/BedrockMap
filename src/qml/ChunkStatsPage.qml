import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "controls"

AppCard {
    id: root
    property var controller
    property int pendingExportRow: -1
    readonly property var statistics: controller ? controller.statistics : []

    FileDialog {
        id: exportDialog
        title: qsTr("Export binary data")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Binary files (*.bin)"), qsTr("All files (*)")]
        onAccepted: {
            const row = root.pendingExportRow
            root.pendingExportRow = -1
            if (!root.controller || !root.controller.exportStatistic(row, String(selectedFile)))
                Notifications.error(qsTr("Could not export binary data."))
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.cardPadding
        spacing: Theme.gapSmall

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 32

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 6
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Name")
                    color: Theme.muted
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    Layout.preferredWidth: 58
                    text: qsTr("Size")
                    color: Theme.muted
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    horizontalAlignment: Text.AlignRight
                }
                Text {
                    Layout.preferredWidth: 70
                    text: qsTr("Hash")
                    color: Theme.muted
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                }
                Text {
                    Layout.preferredWidth: 138
                    text: qsTr("Actions")
                    color: Theme.muted
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                }
            }
        }

        ListView {
            id: statsList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.statistics
            currentIndex: -1
            keyNavigationEnabled: false
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            spacing: 0

            delegate: Item {
                required property var modelData

                width: statsList.width
                height: Math.max(38, Theme.controlHeight + Theme.gapSmall * 2)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 6
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: modelData.name
                        color: Theme.text
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideMiddle
                        verticalAlignment: Text.AlignVCenter
                    }
                    Text {
                        Layout.preferredWidth: 58
                        text: String(modelData.size)
                        color: Theme.muted
                        font.pixelSize: Theme.fontCaption
                        horizontalAlignment: Text.AlignRight
                    }
                    Text {
                        Layout.preferredWidth: 70
                        text: modelData.hash
                        color: Theme.muted
                        font.pixelSize: Theme.fontCaption
                        font.family: "JetBrains Mono"
                    }
                    AppButton {
                        Layout.preferredWidth: 68
                        text: qsTr("View binary")
                        compact: true
                        onClicked: {
                        }
                    }
                    AppButton {
                        Layout.preferredWidth: 60
                        text: qsTr("Export")
                        compact: true
                        onClicked: {
                            root.pendingExportRow = modelData.index
                            exportDialog.open()
                        }
                    }
                }
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: root.statistics.length === 0 && !(root.controller && root.controller.loading)
        text: qsTr("No chunk statistics")
        color: Theme.muted
        font.pixelSize: Theme.fontBody
    }
}

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

Rectangle {
    id: root
    property var controller
    property bool headerVisible: true
    readonly property string headerTitle: controller && controller.hasChunk ? qsTr("Chunk %1, %2").arg(
                                                                                  controller.chunkX).arg(
                                                                                  controller.chunkZ) : qsTr(
                                                                                  "Chunk editor")
    readonly property string headerSubtitle: controller && controller.hasChunk ? qsTr("Dimension %1 · Y %2").arg(
                                                                                     controller.dimension).arg(
                                                                                     controller.yRange) : (controller
                                                                                                           && controller.loading
                                                                                                           ? qsTr("Loading chunk data…") :
                                                                                                             qsTr("No chunk selected"))
    property int category: 0
    readonly property var categoryNames: [qsTr("3D"), qsTr("Terrain"), qsTr("Entities") + (root.controller
                                                                                           && root.controller.entitiesDocument.dirty
                                                                                           ? " *" : ""), qsTr(
            "Block entities") + (root.controller && root.controller.blockEntitiesDocument.dirty ? " *" : ""), qsTr(
            "Pts") + (root.controller && root.controller.pendingTicksDocument.dirty ? " *" : ""), qsTr("HSA"), qsTr(
            "Stats")]
    signal closeRequested

    color: Theme.surface
    border.width: 0

    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        width: 1
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            visible: root.headerVisible
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: Theme.surface

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1

                Text {
                    text: root.headerTitle
                    color: Theme.text
                    font.pixelSize: Theme.fontSubtitle
                    font.bold: true
                }
                Text {
                    text: root.headerSubtitle
                    color: Theme.muted
                    font.pixelSize: Theme.fontCaption
                }
            }

            AppIconButton {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                iconName: "close"
                label: qsTr("Close chunk editor")
                buttonSize: 32
                onClicked: root.closeRequested()
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }
        }

        AppSegmentedControl {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.touch ? Theme.segmentedHeight : 36
            Layout.leftMargin: Theme.gapSmall
            Layout.rightMargin: Theme.gapSmall
            Layout.topMargin: Theme.gapSmall
            options: root.categoryNames
            currentIndex: root.category
            onActivated: index => root.category = index
        }

        StackLayout {
            id: pages
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.gapSmall
            currentIndex: root.category

            VoxelView {
                controller: root.controller
            }
            ChunkTerrainPage {
                controller: root.controller
            }
            ChunkNbtPage {
                document: root.controller ? root.controller.entitiesDocument : null
            }
            ChunkNbtPage {
                document: root.controller ? root.controller.blockEntitiesDocument : null
            }
            ChunkNbtPage {
                document: root.controller ? root.controller.pendingTicksDocument : null
            }
            Item {
                PlaceholderPage {
                    anchors.fill: parent
                    text: qsTr("HSA editing is not available in this preview.")
                }
            }
            ChunkStatsPage {
                controller: root.controller
            }
        }

        AppButton {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapSmall
            Layout.rightMargin: Theme.gapSmall
            Layout.bottomMargin: Theme.gapSmall
            text: qsTr("Save chunk data")
            primary: true
            enabled: root.controller && root.controller.hasChunk && root.controller.dirty
            onClicked: {
                if (!root.controller.saveToCache()) {
                    App.notifications.error(qsTr("Save failed"))
                    return
                }
                App.notifications.info(qsTr("Save successful"))
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.controller && root.controller.loading
        color: "#42000000"
        z: 20

        BusyIndicator {
            anchors.centerIn: parent
            running: parent.visible
        }
    }

    Text {
        anchors.centerIn: parent
        width: parent.width - 32
        visible: root.controller && root.controller.errorMessage.length > 0 && !root.controller.loading
        text: root.controller ? root.controller.errorMessage : ""
        color: Theme.danger
        font.pixelSize: Theme.fontBody
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        z: 21
    }

    component PlaceholderPage: Item {
        property string text

        Text {
            anchors.centerIn: parent
            width: parent.width - 48
            text: parent.text
            color: Theme.muted
            font.pixelSize: Theme.fontBody
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
    }

    component ChunkNbtPage: Item {
        id: nbtPage
        property var document
        property string itemFilter

        NbtDocumentActions {
            id: documentActions
            document: nbtPage.document
            allowSave: false
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.gapSmall

            NbtToolbar {
                Layout.fillWidth: true
                document: nbtPage.document
                allowSave: false
                allowClose: false
                allowAddRemove: true
                showSearch: true
                searchText: nbtPage.itemFilter
                onImportRequested: documentActions.openImport()
                onExportRequested: documentActions.openExport()
                onExportCurrentRequested: documentActions.openExportCurrent()
                onClearRequested: documentActions.openClear()
                onSearchTextEdited: function (text) {
                    nbtPage.itemFilter = text
                }
            }

            NbtEditorSurface {
                Layout.fillWidth: true
                Layout.fillHeight: true
                document: nbtPage.document
                filterText: nbtPage.itemFilter
                itemListTitle: qsTr("Records")
                showItemList: true
            }
        }
    }
}

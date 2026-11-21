import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

Rectangle {
    id: root
    property var controller
    property bool headerVisible: true
    readonly property string headerTitle: {
        if (!root.controller || !root.controller.hasChunk)
            return qsTr("Chunk editor")
        return qsTr("Chunk %1, %2").arg(root.controller.chunkX).arg(root.controller.chunkZ)
    }
    readonly property string headerSubtitle: {
        if (!root.controller)
            return qsTr("No chunk selected")
        if (root.controller.hasChunk)
            return qsTr("Dimension %1 · Y %2").arg(root.controller.dimension).arg(root.controller.yRange)
        return root.controller.loading ? qsTr("Loading chunk data…") : qsTr("No chunk selected")
    }
    function dirtySuffix(document) {
        return document && document.dirty ? " *" : ""
    }
    property int category: 0
    readonly property var categoryNames: [qsTr("3D"), qsTr("Terrain"), qsTr("Entities") + dirtySuffix(root.controller
                                                                                                      ? root.controller.entitiesDocument :
                                                                                                        null), qsTr(
            "Block entities") + dirtySuffix(root.controller ? root.controller.blockEntitiesDocument : null), qsTr("Pts")
        + dirtySuffix(root.controller ? root.controller.pendingTicksDocument : null), qsTr("HSA"), qsTr("Stats")]
    signal closeRequested

    color: Theme.surface
    border.width: 0

    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        width: Theme.borderWidth
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        AppTitleBar {
            visible: root.headerVisible
            embedded: true
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            title: root.headerTitle
            subtitle: root.headerSubtitle
            leftInset: Theme.gapLarge
            rightInset: Theme.gapMedium
            actions: [
                AppIconButton {
                    iconName: "close"
                    label: qsTr("Close chunk editor")
                    buttonSize: Theme.iconButtonSmallSize
                    onClicked: root.closeRequested()
                }
            ]
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
            HsaEditorPage {
                controller: root.controller
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
        width: parent.width - Theme.gapSection * 2
        visible: root.controller && root.controller.errorMessage.length > 0 && !root.controller.loading
        text: root.controller ? root.controller.errorMessage : ""
        color: Theme.danger
        font.pixelSize: Theme.fontBody
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        z: 21
    }

    component ChunkNbtPage: Item {
        id: nbtPage
        property var document
        property string itemFilter
        property bool itemFilterRegex: false

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
                allowAdd: true
                showSearch: true
                regexSearch: nbtPage.itemFilterRegex
                searchText: nbtPage.itemFilter
                onRegexSearchToggleRequested: nbtPage.itemFilterRegex = !nbtPage.itemFilterRegex
                onImportRequested: documentActions.openImport()
                onExportRequested: documentActions.openExport()
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
                filterRegex: nbtPage.itemFilterRegex
                itemListTitle: qsTr("Records")
                showItemList: true
            }
        }
    }
}

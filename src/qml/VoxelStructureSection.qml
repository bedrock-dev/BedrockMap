import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

// Structure import/export, clipboard and delete actions; VoxelPreview runs them.
VoxelPreviewSection {
    id: section

    property var controller: null
    property bool editingEnabled: false
    property bool operationsEnabled: false
    property bool selectionEnabled: false
    readonly property bool importPreviewActive: !!controller && controller.importPreviewActive
    readonly property bool importing: !!controller && controller.importing
    readonly property bool includeEntities: includeEntitiesBox.checked
    readonly property bool includeAir: includeAirBox.checked
    readonly property bool replaceNonAirBlocks: replaceNonAirBlocksBox.checked
    readonly property bool newFormat: newFormatBox.checked

    signal importRequested
    signal exportRequested
    signal copyRequested
    signal pasteRequested
    signal deleteRequested
    signal confirmImportRequested

    component OperationButton: AppButton {
        compact: true
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 0
    }

    objectName: "voxelOperationsSection"
    title: qsTr("Structure")
    contentItem: ColumnLayout {
        spacing: Theme.gapSmall

        GridLayout {
            columns: 2
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            columnSpacing: Theme.gapSmall
            rowSpacing: Theme.gapSmall

            AppCheckBox {
                id: includeEntitiesBox
                objectName: "voxelExportEntities"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                wrapText: true
                text: qsTr("Include entities")
                checked: true
                Accessible.description: qsTr("Applies to all structure operations.")
            }

            AppCheckBox {
                id: includeAirBox
                objectName: "voxelIncludeAir"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                wrapText: true
                text: qsTr("Include air when exporting")
                checked: true
            }

            AppCheckBox {
                id: replaceNonAirBlocksBox
                objectName: "voxelReplaceNonAirBlocks"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                wrapText: true
                text: qsTr("Replace non-air blocks when importing")
                checked: true
                visible: section.editingEnabled
            }

            AppCheckBox {
                id: newFormatBox
                objectName: "voxelExportNewFormat"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                wrapText: true
                text: qsTr("Use new format when exporting")
                checked: false
            }
        }

        RowLayout {
            objectName: "voxelOperationActions"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: Theme.gapSmall

            OperationButton {
                objectName: "voxelExportStructure"
                text: section.controller && section.controller.exporting ? qsTr("Exporting…") : qsTr("Export")
                enabled: section.operationsEnabled
                onClicked: section.exportRequested()
            }
            OperationButton {
                objectName: "voxelCopy"
                text: section.controller && section.controller.copying ? qsTr("Copying…") : qsTr("Copy")
                enabled: section.operationsEnabled
                onClicked: section.copyRequested()
            }
            OperationButton {
                objectName: "voxelImportStructure"
                visible: section.editingEnabled
                text: section.importing ? qsTr("Importing…") : qsTr("Import")
                enabled: section.operationsEnabled
                onClicked: section.importRequested()
            }
            OperationButton {
                objectName: "voxelPaste"
                visible: section.editingEnabled
                text: qsTr("Paste")
                enabled: section.operationsEnabled
                onClicked: section.pasteRequested()
            }
            OperationButton {
                objectName: "voxelDelete"
                visible: section.editingEnabled
                danger: true
                text: section.controller && section.controller.deleting ? qsTr("Deleting…") : qsTr("Delete")
                enabled: section.operationsEnabled && section.selectionEnabled
                onClicked: section.deleteRequested()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            visible: section.importPreviewActive
            spacing: Theme.gapSmall

            Item {
                Layout.fillWidth: true
            }

            AppButton {
                compact: true
                text: qsTr("Cancel")
                enabled: !section.importing
                onClicked: section.controller.cancelImport()
            }

            AppButton {
                compact: true
                text: section.importing ? qsTr("Importing…") : qsTr("Confirm import")
                primary: true
                enabled: !section.importing && section.selectionEnabled
                onClicked: section.confirmImportRequested()
            }
        }
    }
}

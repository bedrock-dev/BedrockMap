import BedrockMap 1.0
import QtQuick
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
    readonly property bool newFormat: structureFormat.currentIndex === 1

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

        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: Theme.gapSmall

            AppCheckBox {
                id: includeEntitiesBox
                objectName: "voxelExportEntities"
                text: qsTr("Include entities")
                checked: true
                Accessible.description: qsTr("Applies to structure and clipboard operations.")
            }

            Item {
                Layout.fillWidth: true
            }

            AppComboBox {
                id: structureFormat
                objectName: "voxelExportFormat"
                Layout.preferredWidth: Theme.ui(Theme.touch ? 112 : 130)
                Layout.minimumWidth: 96
                model: [qsTr("Old format"), qsTr("New format")]
                Accessible.name: qsTr("Structure format")
            }
        }

        RowLayout {
            objectName: "voxelOperationActions"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: Theme.gapSmall

            OperationButton {
                objectName: "voxelImportStructure"
                text: section.importing ? qsTr("Importing…") : qsTr("Import")
                visible: section.editingEnabled
                enabled: section.operationsEnabled
                onClicked: section.importRequested()
            }
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
                objectName: "voxelPaste"
                text: qsTr("Paste")
                visible: section.editingEnabled
                enabled: section.operationsEnabled
                onClicked: section.pasteRequested()
            }
            OperationButton {
                objectName: "voxelDelete"
                text: section.controller && section.controller.deleting ? qsTr("Deleting…") : qsTr("Delete")
                visible: section.editingEnabled
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

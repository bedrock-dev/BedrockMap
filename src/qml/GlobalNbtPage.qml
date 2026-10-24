import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "controls"

Item {
    id: page
    property var section
    property string itemFilter: ""
    property var document: section ? section.document : null
    property string pendingImportPath
    readonly property bool singleRecord: !!section && section.singleRecord
    readonly property bool compact: Theme.nbtCompact

    function performImport(path) {
        if (!document || !document.importFile(path))
            Notifications.error(document ? document.lastError : qsTr("No NBT document is open."))
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import NBT file")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("NBT files (*.nbt *.nbts *.mcstructure *.palette)"), qsTr("All files (*)")]
        onAccepted: {
            const path = String(selectedFile)
            if (document && document.hasDocument && !document.isEmpty()) {
                page.pendingImportPath = path
                importConfirmDialog.open()
            } else if (document) {
                page.performImport(path)
            }
        }
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export NBT file")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "nbt"
        nameFilters: [qsTr("NBT files (*.nbt *.nbts *.mcstructure *.palette)"), qsTr("All files (*)")]
        onAccepted: {
            if (!document.exportFile(String(selectedFile)))
                Notifications.error(document.lastError)
        }
    }
    ConfirmDialog {
        id: importConfirmDialog
        title: qsTr("Replace NBT records")
        message: qsTr("Importing will replace the current NBT records. Unsaved changes will be lost.")
        confirmText: qsTr("Import")
        danger: true
        onConfirmed: {
            const path = page.pendingImportPath
            page.pendingImportPath = ""
            page.performImport(path)
        }
        onCancelled: page.pendingImportPath = ""
    }
    ConfirmDialog {
        id: clearConfirmDialog
        title: qsTr("Clear all records")
        message: qsTr("Remove all records from this NBT section? This cannot be undone.")
        confirmText: qsTr("Clear")
        danger: true
        onConfirmed: if (!document.clearContents())
                         Notifications.error(document.lastError)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.cardPadding
        anchors.rightMargin: Theme.cardPadding
        anchors.topMargin: Theme.pageGutter
        anchors.bottomMargin: Theme.pageGutter
        spacing: Theme.gapMedium

        NbtToolbar {
            Layout.fillWidth: true
            document: page.document
            compact: page.compact
            allowSave: false
            allowClose: false
            allowAddRemove: !page.singleRecord
            showSearch: !page.singleRecord
            searchText: page.itemFilter
            onImportRequested: importDialog.open()
            onExportRequested: exportDialog.open()
            onClearRequested: clearConfirmDialog.open()
            onSearchTextEdited: function (text) {
                page.itemFilter = text
            }
        }

        NbtEditorSurface {
            Layout.fillWidth: true
            Layout.fillHeight: true
            document: page.document
            compact: page.compact
            filterText: page.itemFilter
            itemListTitle: qsTr("Records")
            showItemList: !page.singleRecord
        }
    }
}

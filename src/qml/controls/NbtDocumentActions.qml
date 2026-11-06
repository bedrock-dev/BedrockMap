import QtQuick
import QtQuick.Dialogs
import BedrockMap 1.0

Item {
    id: root
    property var document
    property bool allowSave: true
    property bool allowReplace: false
    property bool externalSave: false
    signal saveRequested

    function report(error) {
        App.notifications.error(error || (root.document ? root.document.lastError : qsTr("No NBT document is open.")))
    }
    function openImport() {
        if (root.document && !root.document.readOnly)
            importDialog.open()
    }
    function openReplace() {
        if (root.document && !root.document.readOnly && root.allowReplace)
            replaceDialog.open()
    }
    function openClear() {
        if (root.document && !root.document.readOnly && root.document.rootCount > 0)
            clearDialog.open()
    }
    function openExport() {
        if (root.document)
            exportDialog.open()
    }
    function openExportCurrent() {
        if (root.document && root.document.currentIndex >= 0)
            exportCurrentDialog.open()
    }
    function saveDocument() {
        if (!root.document || !root.document.hasDocument || root.document.readOnly || !root.allowSave)
            return
        if (root.externalSave) {
            root.saveRequested()
            return
        }
        if (root.document.requiresSaveAs) {
            saveDialog.open()
            return
        }
        if (!root.document.save())
            App.notifications.error(qsTr("Save failed"))
    }

    FileUrlDialog {
        id: importDialog
        title: qsTr("Import NBT file")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("NBT files (*.nbt *.nbts *.mcstructure *.palette)"), qsTr("All files (*)")]
        onAccepted: if (!root.document.importFile(selectedUrl))
                        App.notifications.error(qsTr("Import failed"))
                    else
                        App.notifications.info(qsTr("Import successful"))
    }
    FileUrlDialog {
        id: replaceDialog
        title: qsTr("Replace NBT record")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("NBT files (*.nbt *.nbts *.mcstructure *.palette)"), qsTr("All files (*)")]
        onAccepted: if (!root.document.replaceFile(selectedUrl))
                        App.notifications.error(qsTr("Import failed"))
                    else
                        App.notifications.info(qsTr("Import successful"))
    }
    FileUrlDialog {
        id: saveDialog
        title: qsTr("Save NBT file")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "nbt"
        nameFilters: [qsTr("NBT files (*.nbt *.nbts *.mcstructure *.palette)"), qsTr("All files (*)")]
        onAccepted: if (!root.document.saveAs(selectedUrl))
                        App.notifications.error(qsTr("Save failed"))
    }
    FileUrlDialog {
        id: exportDialog
        title: qsTr("Export NBT file")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "nbt"
        nameFilters: [qsTr("NBT files (*.nbt *.nbts *.mcstructure *.palette)"), qsTr("All files (*)")]
        onAccepted: if (!root.document.exportFile(selectedUrl))
                        App.notifications.error(qsTr("Export failed"))
                    else
                        App.notifications.info(qsTr("Export successful"))
    }
    FileUrlDialog {
        id: exportCurrentDialog
        title: qsTr("Export current NBT record")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "nbt"
        nameFilters: [qsTr("NBT files (*.nbt *.nbts *.mcstructure *.palette)"), qsTr("All files (*)")]
        onAccepted: if (!root.document.exportCurrent(selectedUrl))
                        App.notifications.error(qsTr("Export failed"))
                    else
                        App.notifications.info(qsTr("Export successful"))
    }
    ConfirmDialog {
        id: clearDialog
        title: qsTr("Clear all records")
        message: qsTr("Remove all records from this NBT section? This cannot be undone.")
        confirmText: qsTr("Clear")
        danger: true
        onConfirmed: if (!root.document.clearContents())
                         root.report()
    }
}

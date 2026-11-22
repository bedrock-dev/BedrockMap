import QtQuick
import QtQuick.Dialogs
import BedrockMap 1.0

Item {
    id: root
    property var document
    property bool allowReplace: false

    function report(title, error) {
        const fallback = root.document ? "" : qsTr("No NBT document is open.")
        AppErrorReporter.report(title, root.document, error || fallback)
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
        if (!root.document)
            return
        const fileName = root.document.suggestedSaveFileName()
        exportDialog.selectedFile = FileDialogSupport.selectedUrl(exportDialog, fileName)
        exportDialog.open()
    }
    FileUrlDialog {
        id: importDialog
        title: qsTr("Import NBT file")
        fileMode: FileDialog.OpenFile
        nameFilters: FileDialogSupport.nbtFilters
        onAccepted: if (!root.document.importFile(selectedUrl))
                        root.report(qsTr("Import failed"))
                    else
                        App.notifications.info(qsTr("Import successful"))
    }
    FileUrlDialog {
        id: replaceDialog
        title: qsTr("Replace NBT record")
        fileMode: FileDialog.OpenFile
        nameFilters: FileDialogSupport.nbtFilters
        onAccepted: if (!root.document.replaceFile(selectedUrl))
                        root.report(qsTr("Import failed"))
                    else
                        App.notifications.info(qsTr("Import successful"))
    }
    FileUrlDialog {
        id: exportDialog
        title: qsTr("Export NBT file")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "nbt"
        nameFilters: FileDialogSupport.nbtFilters
        onAccepted: if (!root.document.exportFile(selectedUrl))
                        root.report(qsTr("Export failed"))
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
                         root.report(qsTr("Clear failed"))
    }
}

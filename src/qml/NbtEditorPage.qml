import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import BedrockMap 1.0
import "controls"

Item {
    id: page
    property var document
    property bool compact: Theme.nbtCompact
    property string itemFilter: ""
    property string pendingImportPath
    signal closeRequested

    function performImport(path) {
        if (!document || !document.loadFile(path))
            Notifications.error(document ? document.lastError : qsTr("No NBT document is open."))
    }
    function saveDocument() {
        if (!document || !document.hasDocument || document.readOnly)
            return
        if (!document.save())
            saveDialog.open()
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
            } else {
                page.performImport(path)
            }
        }
    }
    ConfirmDialog {
        id: importConfirmDialog
        title: qsTr("Replace NBT document")
        message: qsTr("Importing will replace the current NBT document. Unsaved changes in this tab will be lost.")
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
        message: qsTr("Remove all NBT records from this document? This cannot be undone.")
        confirmText: qsTr("Clear")
        danger: true
        onConfirmed: if (!document.clearContents())
                         Notifications.error(document.lastError)
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save NBT file")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "nbt"
        nameFilters: [qsTr("NBT files (*.nbt *.nbts *.mcstructure *.palette)"), qsTr("All files (*)")]
        onAccepted: {
            if (!document.saveAs(selectedFile))
                Notifications.error(document.lastError)
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

    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: (page.compact ? Theme.topbarHeight : 0)
                                   + (document.hasDocument ? fileToolbar.implicitHeight + Theme.cardPadding * 2 + 1 : 0)
            color: Theme.surface

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                AppTitleBar {
                    visible: page.compact
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.topbarHeight
                    title: document.documentTitle + (document.dirty ? " *" : "")

                    actions: [
                        AppIconButton {
                            Layout.alignment: Qt.AlignVCenter
                            iconName: "save"
                            label: qsTr("Save NBT document")
                            buttonSize: Theme.touch ? Theme.touchTarget : 30
                            enabled: document.hasDocument && !document.readOnly
                            onClicked: page.saveDocument()
                        },
                        AppIconButton {
                            Layout.alignment: Qt.AlignVCenter
                            iconName: "exit"
                            label: qsTr("Close NBT document")
                            danger: true
                            buttonSize: Theme.touch ? Theme.touchTarget : 30
                            onClicked: page.closeRequested()
                        }
                    ]
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: document.hasDocument ? fileToolbar.implicitHeight + Theme.cardPadding * 2 : 0
                    visible: document.hasDocument

                    NbtToolbar {
                        id: fileToolbar
                        anchors.fill: parent
                        anchors.margins: Theme.cardPadding
                        document: page.document
                        compact: page.compact
                        allowSave: true
                        allowClose: true
                        showSearch: false
                        searchText: page.itemFilter
                        onSearchTextEdited: function (text) {
                            page.itemFilter = text
                        }
                        onImportRequested: importDialog.open()
                        onExportRequested: exportDialog.open()
                        onClearRequested: clearConfirmDialog.open()
                        onSaveRequested: page.saveDocument()
                        onCloseRequested: page.closeRequested()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: document.hasDocument ? 1 : 0
                    color: Theme.border
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(460, parent.width - 40)
                spacing: 10
                visible: !document.hasDocument
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("NBT editor")
                    color: Theme.text
                    font.pixelSize: Theme.fontTitle
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Import an NBT, NBTS, mcstructure or palette file to inspect and edit its tree.")
                    color: Theme.muted
                    font.pixelSize: Theme.fontSubtitle
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    AppButton {
                        primary: true
                        text: qsTr("Import file")
                        onClicked: importDialog.open()
                    }
                    AppButton {
                        text: qsTr("New document")
                        onClicked: document.createNew()
                    }
                }
            }

            NbtEditorSurface {
                anchors.fill: parent
                anchors.leftMargin: Theme.cardPadding
                anchors.rightMargin: Theme.cardPadding
                anchors.topMargin: Theme.gapSmall
                anchors.bottomMargin: Theme.gapSmall
                visible: document.hasDocument
                document: page.document
                compact: page.compact
                filterText: page.itemFilter
                itemListTitle: qsTr("NBT roots")
            }
        }
    }
}

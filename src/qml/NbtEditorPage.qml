import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BedrockMap 1.0
import "controls"

Item {
    id: page
    property var document
    property bool compact: Theme.compact
    property string itemFilter: ""
    readonly property real toolbarVerticalPadding: Theme.touch ? Theme.gapSmall : Theme.cardPadding
    signal closeRequested
    signal saveRequested

    NbtDocumentActions {
        id: documentActions
        document: page.document
        allowSave: true
        externalSave: true
        onSaveRequested: page.saveRequested()
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
            Layout.preferredHeight: document.hasDocument ? fileToolbar.implicitHeight + page.toolbarVerticalPadding * 2
                                                           + 1 : 0
            color: Theme.surface

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: document.hasDocument ? fileToolbar.implicitHeight
                                                                   + page.toolbarVerticalPadding * 2 : 0

                    visible: document.hasDocument

                    NbtToolbar {
                        id: fileToolbar
                        anchors.fill: parent
                        anchors.leftMargin: Theme.cardPadding
                        anchors.rightMargin: Theme.cardPadding
                        anchors.topMargin: page.toolbarVerticalPadding
                        anchors.bottomMargin: page.toolbarVerticalPadding
                        document: page.document
                        compact: page.compact
                        // The shell title bar carries Save and Close when shown.
                        inlineFileActions: !Theme.showTopbar
                        allowSave: true
                        allowClose: true
                        showSearch: false
                        searchText: page.itemFilter
                        onSearchTextEdited: function (text) {
                            page.itemFilter = text
                        }
                        onImportRequested: documentActions.openImport()
                        onExportRequested: documentActions.openExport()
                        onExportCurrentRequested: documentActions.openExportCurrent()
                        onClearRequested: documentActions.openClear()
                        onSaveRequested: page.saveRequested()
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
                        onClicked: documentActions.openImport()
                    }
                    AppButton {
                        text: qsTr("New document")
                        onClicked: document.createNew()
                    }
                }
            }

            NbtEditorSurface {
                anchors.fill: parent
                anchors.margins: Theme.gapSmall
                visible: document.hasDocument
                document: page.document
                compact: page.compact
                filterText: page.itemFilter
                itemListTitle: qsTr("NBT roots")
            }
        }
    }
}

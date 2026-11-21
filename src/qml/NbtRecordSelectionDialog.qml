pragma ComponentBehavior: Bound

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "controls"

AppDialog {
    id: root

    property var document: null
    property var items: []
    property var selectedValues: []
    readonly property int selectedCount: selectedValues.length

    preferredWidth: Theme.ui(460)

    function openFor(editorDocument, newItems) {
        document = editorDocument
        items = newItems || []
        selectedValues = []
        open()
    }
    function isSelected(value) {
        return selectedValues.indexOf(value) >= 0
    }
    function toggleValue(value) {
        const next = selectedValues.slice()
        const position = next.indexOf(value)
        if (position >= 0)
            next.splice(position, 1)
        else
            next.push(value)
        selectedValues = next
    }
    function selectAll() {
        selectedValues = items.map(item => item.value)
    }
    function invertSelection() {
        const current = selectedValues
        selectedValues = items.map(item => item.value).filter(value => current.indexOf(value) < 0)
    }

    component DialogButton: AppButton {
        compact: true
        Layout.minimumWidth: Theme.ui(96)
        Layout.preferredWidth: Theme.ui(96)
    }

    FileUrlDialog {
        id: exportDialog
        title: qsTr("Export selected records")
        fileMode: FileDialog.SaveFile
        defaultSuffix: root.selectedCount > 1 ? "nbts" : "nbt"
        nameFilters: [qsTr("NBT files (*.nbt *.nbts)"), qsTr("All files (*)")]
        onAccepted: {
            if (!root.document || !root.document.exportItems(root.selectedValues, selectedUrl)) {
                App.notifications.error(qsTr("Export failed"))
                return
            }
            App.notifications.info(qsTr("Export successful"))
            root.close()
        }
    }
    ConfirmDialog {
        id: deleteDialog
        title: qsTr("Delete selected records")
        message: qsTr("Delete %1 selected records?").arg(root.selectedCount)
        confirmText: qsTr("Delete")
        danger: true
        onConfirmed: {
            if (!root.document || !root.document.removeItems(root.selectedValues)) {
                App.notifications.error(qsTr("Delete failed"))
                return
            }
            root.close()
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.gapMedium

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapSmall

            Text {
                Layout.fillWidth: true
                text: qsTr("%1 of %2 records selected").arg(root.selectedCount).arg(root.items.length)
                color: Theme.text
                font.pixelSize: Theme.fontSubtitle
                elide: Text.ElideRight
            }
            DialogButton {
                text: qsTr("Select all")
                enabled: root.items.length > 0
                onClicked: root.selectAll()
            }
            DialogButton {
                text: qsTr("Invert")
                enabled: root.items.length > 0
                onClicked: root.invertSelection()
            }
        }
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.ui(320)
            clip: true
            model: root.items
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }
            delegate: AppCheckBox {
                id: recordCheckBox
                required property int index
                required property var modelData
                width: ListView.view.width
                height: implicitHeight
                text: recordCheckBox.modelData.text
                wrapText: true
                onToggled: root.toggleValue(recordCheckBox.modelData.value)
                Binding {
                    target: recordCheckBox
                    property: "checked"
                    value: root.isSelected(recordCheckBox.modelData.value)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapSmall

            Item {
                Layout.fillWidth: true
            }
            DialogButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            DialogButton {
                text: qsTr("Export")
                primary: true
                enabled: root.selectedCount > 0 && !!root.document
                onClicked: exportDialog.open()
            }
            DialogButton {
                text: qsTr("Delete")
                danger: true
                enabled: root.selectedCount > 0 && !!root.document && !root.document.readOnly
                onClicked: deleteDialog.open()
            }
        }
    }
}

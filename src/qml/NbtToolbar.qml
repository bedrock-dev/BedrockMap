import BedrockMap 1.0
import QtQuick
import QtQuick.Layouts
import "controls"

Rectangle {
    id: root
    property var document
    property bool allowImport: true
    property bool allowReplace: false
    property bool allowExport: true
    property bool allowExportCurrent: true
    property bool allowSave: true
    property bool allowClose: true
    property bool allowAddRemove: true
    property bool allowClear: allowAddRemove
    property bool showSearch: true
    property bool allowDetailsToggle: false
    property bool detailsHidden: false
    property bool compact: Theme.compact
    // Save and Close sit in the toolbar unless the host shows them elsewhere.
    property bool inlineFileActions: !compact
    property string searchText
    signal importRequested
    signal replaceRequested
    signal exportRequested
    signal exportCurrentRequested
    signal clearRequested
    signal saveRequested
    signal closeRequested
    signal searchTextEdited(string text)
    signal detailsToggleRequested

    readonly property real gap: Theme.gapSmall
    readonly property real rowGap: Theme.gapSmall
    readonly property real actionHeight: compact ? Theme.compactButtonHeight : Theme.buttonHeight
    readonly property real searchHeight: Math.max(Theme.buttonHeight, root.allowDetailsToggle
                                                  ? detailsToggle.controlSize : 0)
    readonly property int importActionCount: root.allowImport ? 1 : 0
    readonly property int replaceActionCount: root.allowReplace ? 1 : 0
    readonly property int exportActionCount: root.allowExport ? 1 : 0
    readonly property int exportCurrentActionCount: root.allowExportCurrent ? 1 : 0
    readonly property int addRemoveActionCount: root.allowAddRemove ? 2 : 0
    readonly property int clearActionCount: root.allowClear ? 1 : 0
    readonly property int saveActionCount: root.inlineFileActions && root.allowSave ? 1 : 0
    readonly property int closeActionCount: root.inlineFileActions && root.allowClose ? 1 : 0
    readonly property int actionCount: importActionCount + replaceActionCount + exportActionCount
                                       + exportCurrentActionCount + addRemoveActionCount + clearActionCount
                                       + saveActionCount + closeActionCount
    readonly property real importButtonWidth: root.allowImport ? importButton.implicitWidth : 0
    readonly property real replaceButtonWidth: root.allowReplace ? replaceButton.implicitWidth : 0
    readonly property real exportButtonWidth: root.allowExport ? exportButton.implicitWidth : 0
    readonly property real exportCurrentButtonWidth: root.allowExportCurrent ? exportCurrentButton.implicitWidth : 0
    readonly property real addRemoveButtonWidth: root.allowAddRemove ? Math.max(addButton.implicitWidth,
                                                                                deleteButton.implicitWidth) : 0
    readonly property real clearButtonWidth: root.allowClear ? clearButton.implicitWidth : 0
    readonly property real saveButtonWidth: root.saveActionCount > 0 ? saveButton.implicitWidth : 0
    readonly property real closeButtonWidth: root.closeActionCount > 0 ? closeButton.implicitWidth : 0
    readonly property real naturalButtonWidth: Math.max(importButtonWidth, replaceButtonWidth, exportButtonWidth,
                                                        exportCurrentButtonWidth, addRemoveButtonWidth, clearButtonWidth,
                                                        saveButtonWidth, closeButtonWidth)
    readonly property real naturalActionsWidth: actionCount * naturalButtonWidth + Math.max(0, actionCount - 1) * gap
    readonly property real detailsToggleSpace: root.allowDetailsToggle ? detailsToggle.controlSize + root.gap : 0
    readonly property real inlineSearchMinimumWidth: 160 * Theme.metricScale + detailsToggleSpace + root.gap
                                                     + naturalActionsWidth
    // Measure this toolbar, including translated labels, rather than the window.
    readonly property bool inlineSearch: root.showSearch && root.actionCount > 0 && root.width
                                         >= root.inlineSearchMinimumWidth
    readonly property real actionsWidth: Math.max(0, Math.min(width, naturalActionsWidth))
    readonly property real columnWidth: Math.max(1, naturalButtonWidth + gap)
    readonly property int columnCapacity: Math.max(1, Math.floor((actionsWidth + gap) / columnWidth))
    readonly property int actionRows: actionCount > 0 ? Math.ceil(actionCount / columnCapacity) : 0
    // Balance rows (3 + 2 instead of 4 + 1) without changing action order.
    readonly property int actionColumns: actionRows > 0 ? Math.ceil(actionCount / actionRows) : 1
    readonly property real actionWidth: Math.max(0, (actionsWidth - (actionColumns - 1) * gap) / actionColumns)
    readonly property real actionsHeight: actionRows * actionHeight + Math.max(0, actionRows - 1) * rowGap
    readonly property real searchGap: root.showSearch && root.actionCount > 0 ? root.gap : 0
    readonly property real searchSectionHeight: root.showSearch ? root.searchHeight + root.searchGap : 0
    readonly property real stackedHeight: root.searchSectionHeight + root.actionsHeight
    readonly property real actionsY: root.inlineSearch ? (root.implicitHeight - root.actionsHeight) / 2 :
                                                         root.searchSectionHeight

    implicitHeight: root.inlineSearch ? Math.max(root.searchHeight, root.actionsHeight) : root.stackedHeight
    Layout.minimumWidth: 0
    Layout.preferredWidth: 0
    clip: true
    color: "transparent"

    RowLayout {
        visible: root.showSearch
        width: root.inlineSearch ? root.width - root.actionsWidth - root.gap : root.width
        height: root.searchHeight
        y: root.inlineSearch ? (root.implicitHeight - height) / 2 : 0
        spacing: root.gap

        AppTextField {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: root.searchHeight
            text: root.searchText
            placeholderText: qsTr("Search")
            onTextEdited: root.searchTextEdited(text)
        }
        AppIconButton {
            id: detailsToggle
            visible: root.allowDetailsToggle
            iconName: "nbt_editor"
            label: root.detailsHidden ? qsTr("Show NBT details") : qsTr("Hide NBT details")
            selected: root.detailsHidden
            buttonSize: Theme.buttonHeight
            onClicked: root.detailsToggleRequested()
        }
    }

    component ActionButton: AppButton {
        compact: root.compact
        width: root.actionWidth
        height: root.actionHeight
        visualHeight: height
    }

    Grid {
        x: root.inlineSearch ? root.width - width : 0
        y: root.actionsY
        width: root.actionsWidth
        columns: root.actionColumns
        columnSpacing: root.gap
        rowSpacing: root.rowGap

        ActionButton {
            id: importButton
            text: qsTr("Import")
            visible: root.allowImport
            enabled: root.document && !root.document.readOnly
            onClicked: root.importRequested()
        }
        ActionButton {
            id: replaceButton
            text: qsTr("Replace")
            visible: root.allowReplace
            enabled: root.document && !root.document.readOnly
            onClicked: root.replaceRequested()
        }
        ActionButton {
            id: exportButton
            text: qsTr("Export")
            visible: root.allowExport
            enabled: !!root.document
            onClicked: root.exportRequested()
        }
        ActionButton {
            id: exportCurrentButton
            text: qsTr("Export current")
            visible: root.allowExportCurrent
            enabled: !!root.document && root.document.rootCount > 0 && root.document.currentIndex >= 0
            onClicked: root.exportCurrentRequested()
        }
        ActionButton {
            id: addButton
            text: qsTr("Add")
            visible: root.allowAddRemove
            enabled: root.document && !root.document.readOnly
            onClicked: if (root.document)
                           root.document.addItem()
        }
        ActionButton {
            id: deleteButton
            text: qsTr("Delete")
            visible: root.allowAddRemove
            enabled: root.document && !root.document.readOnly && root.document.rootCount > 0
            onClicked: if (root.document)
                           root.document.removeItem(root.document.currentIndex)
        }
        ActionButton {
            id: clearButton
            text: qsTr("Clear")
            visible: root.allowClear
            enabled: root.document && !root.document.readOnly && root.document.rootCount > 0
            onClicked: root.clearRequested()
        }
        ActionButton {
            id: saveButton
            text: qsTr("Save")
            visible: root.inlineFileActions && root.allowSave
            primary: true
            enabled: root.document && root.document.hasDocument && !root.document.readOnly
            onClicked: root.saveRequested()
        }
        ActionButton {
            id: closeButton
            text: qsTr("Close")
            visible: root.inlineFileActions && root.allowClose
            danger: true
            onClicked: root.closeRequested()
        }
    }
}

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
    readonly property real rowGap: Theme.toolbarRowSpacing
    readonly property real actionHeight: compact ? Theme.compactButtonHeight : Theme.buttonHeight
    readonly property real searchHeight: Math.max(Theme.buttonHeight, allowDetailsToggle ? detailsToggle.controlSize : 0)
    readonly property int actionCount: (allowImport ? 1 : 0) + (allowReplace ? 1 : 0) + (allowExport ? 1 : 0) + (
                                           allowExportCurrent ? 1 : 0) + (allowAddRemove ? 2 : 0) + (allowClear ? 1 :
                                                                                                                  0) + (inlineFileActions
                                                                                                                        && allowSave
                                                                                                                        ? 1 : 0)
                                       + (inlineFileActions && allowClose ? 1 : 0)
    readonly property real naturalButtonWidth: Math.max(allowImport ? importButton.implicitWidth : 0, allowReplace
                                                        ? replaceButton.implicitWidth : 0, allowExport
                                                        ? exportButton.implicitWidth : 0, allowExportCurrent
                                                        ? exportCurrentButton.implicitWidth : 0, allowAddRemove
                                                        ? Math.max(addButton.implicitWidth, deleteButton.implicitWidth) :
                                                          0, allowClear ? clearButton.implicitWidth : 0, inlineFileActions
                                                        && allowSave ? saveButton.implicitWidth : 0, inlineFileActions
                                                        && allowClose ? closeButton.implicitWidth : 0)
    readonly property real naturalActionsWidth: actionCount * naturalButtonWidth + Math.max(0, actionCount - 1) * gap
    // Measure this toolbar, including translated labels, rather than the window.
    readonly property bool inlineSearch: showSearch && actionCount > 0 && width >= 160 * Theme.metricScale
                                         + (allowDetailsToggle ? detailsToggle.controlSize + gap : 0) + gap
                                         + naturalActionsWidth
    readonly property real actionsWidth: Math.max(0, Math.min(width, naturalActionsWidth))
    readonly property int columnCapacity: Math.max(1, Math.floor((actionsWidth + gap) / Math.max(1, naturalButtonWidth
                                                                                                 + gap)))
    readonly property int actionRows: actionCount > 0 ? Math.ceil(actionCount / columnCapacity) : 0
    // Balance rows (3 + 2 instead of 4 + 1) without changing action order.
    readonly property int actionColumns: actionRows > 0 ? Math.ceil(actionCount / actionRows) : 1
    readonly property real actionWidth: Math.max(0, (actionsWidth - (actionColumns - 1) * gap) / actionColumns)
    readonly property real actionsHeight: actionRows * actionHeight + Math.max(0, actionRows - 1) * rowGap

    implicitHeight: inlineSearch ? Math.max(searchHeight, actionsHeight) : (showSearch ? searchHeight + (actionCount > 0
                                                                                                         ? gap : 0) :
                                                                                         0) + actionsHeight
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
        y: root.inlineSearch ? (root.implicitHeight - root.actionsHeight) / 2 : (root.showSearch ? root.searchHeight
                                                                                                   + root.gap : 0)
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
            enabled: !!root.document && root.document.itemCount > 0 && root.document.currentIndex >= 0
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
            enabled: root.document && !root.document.readOnly && root.document.itemCount > 1
            onClicked: if (root.document)
                           root.document.removeItem(root.document.currentIndex)
        }
        ActionButton {
            id: clearButton
            text: qsTr("Clear")
            visible: root.allowClear
            enabled: root.document && !root.document.readOnly && root.document.itemCount > 0
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

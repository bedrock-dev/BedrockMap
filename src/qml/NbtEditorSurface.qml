import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

Item {
    id: root
    property var document
    property bool compact: Theme.compact
    property string filterText
    property string itemListTitle: qsTr("NBT roots")
    property bool showItemList: true
    property bool detailsHidden: false
    property bool readOnly: false
    property int contextRow: -1
    property var contextInfo: ({})

    onDetailsHiddenChanged: {
        if (!detailsHidden)
            return
        if (nodeMenu)
            nodeMenu.close()
        if (tagDialog)
            tagDialog.close()
        if (clearDialog)
            clearDialog.close()
    }

    NbtTagDialog {
        id: tagDialog
        onCommitRequested: function (type, key, value) {
            const ok = tagDialog.mode === 0 ? root.document.treeModel.addChild(tagDialog.sourceRow, type, key, value) :
                                              root.document.treeModel.updateNode(tagDialog.sourceRow, key, value)
            if (!ok) {
                App.notifications.error(root.document.treeModel.lastError)
                return
            }
            tagDialog.close()
        }
    }
    ConfirmDialog {
        id: clearDialog
        title: qsTr("Clear children")
        message: qsTr("Remove all children of this tag?")
        confirmText: qsTr("Clear")
        danger: true
        width: Math.min(360, parent ? parent.width - 32 : 360)
        onConfirmed: if (!root.document.treeModel.clearChildren(root.contextRow))
                         App.notifications.error(root.document.treeModel.lastError)
    }
    AppMenu {
        id: nodeMenu
        AppMenuItem {
            id: addAction
            text: qsTr("Add child")
            visible: root.contextInfo.canAdd === true
            onTriggered: tagDialog.openCreate(root.contextRow, root.contextInfo.childType === undefined ? -1 :
                                                                                                          root.contextInfo.childType)
        }
        AppMenuItem {
            id: editAction
            text: qsTr("Edit")
            visible: root.contextInfo.canEdit === true
            onTriggered: tagDialog.openModify(root.contextRow, root.contextInfo)
        }
        AppMenuSeparator {
            visible: (addAction.visible || editAction.visible) && (removeAction.visible || clearAction.visible)
        }
        AppMenuItem {
            id: removeAction
            danger: true
            text: qsTr("Remove")
            visible: root.contextInfo.canRemove === true
            onTriggered: {
                if (!root.document.treeModel.removeNode(root.contextRow))
                    App.notifications.error(root.document.treeModel.lastError)
            }
        }
        AppMenuItem {
            id: clearAction
            text: qsTr("Clear children")
            visible: root.contextInfo.canClear === true
            onTriggered: clearDialog.open()
        }
    }

    Connections {
        target: root.document
        function onLastErrorChanged() {
            if (root.document.lastError.length > 0)
                App.notifications.error(root.document.lastError)
        }
    }
    Connections {
        target: root.document ? root.document.treeModel : null
        function onModelReset() {
            root.contextRow = -1
        }
    }

    NbtDocumentView {
        anchors.fill: parent
        document: root.document
        compact: root.compact
        filterText: root.filterText
        itemListTitle: root.itemListTitle
        showItemList: root.showItemList
        detailsHidden: root.detailsHidden
        onEditRequested: function (row) {
            if (root.readOnly || root.detailsHidden)
                return
            root.contextRow = row
            root.contextInfo = root.document.treeModel.tagInfo(row)
            tagDialog.openModify(row, root.contextInfo)
        }
        onContextRequested: function (row, sceneX, sceneY) {
            if (root.readOnly || root.detailsHidden)
                return
            root.contextRow = row
            root.contextInfo = root.document.treeModel.tagInfo(row)
            if (!root.contextInfo.canAdd && !root.contextInfo.canEdit && !root.contextInfo.canRemove &&
                    !root.contextInfo.canClear)
                return
            const pos = root.mapFromItem(null, sceneX, sceneY)
            nodeMenu.popup(pos.x, pos.y)
        }
    }
}

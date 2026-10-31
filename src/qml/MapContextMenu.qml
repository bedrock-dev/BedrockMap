import QtQuick
import BedrockMap 1.0
import "controls"

AppMenu {
    id: root

    property bool insideSelection: false
    property int selectionRects: 0
    property int dimension: 0
    property var blockInfo: ({})
    property bool operationEnabled: false
    property bool inputBlocked: true
    property bool importActive: false

    signal unselectRequested
    signal deleteSelectionRequested
    signal createVoidRequested
    signal setBiomeRequested
    signal copySelectionRequested
    signal exportSelectionRequested
    signal pasteRequested
    signal importRequested
    signal copyTextRequested(string text)
    signal screenshotRequested
    signal voxelViewRequested
    signal chunkEditorRequested

    AppMenuItem {
        visible: root.insideSelection
        text: qsTr("Unselect")
        onTriggered: root.unselectRequested()
    }

    AppMenu {
        title: qsTr("Selection operations")
        menuItemVisible: root.insideSelection

        AppMenuItem {
            text: qsTr("Delete")
            enabled: root.operationEnabled
            onTriggered: root.deleteSelectionRequested()
        }
        AppMenuItem {
            text: qsTr("Create void")
            enabled: root.operationEnabled
            onTriggered: root.createVoidRequested()
        }
        AppMenuItem {
            text: qsTr("Set biome")
            enabled: root.operationEnabled
            onTriggered: root.setBiomeRequested()
        }
        AppMenuItem {
            text: qsTr("Copy")
            enabled: root.operationEnabled
            onTriggered: root.copySelectionRequested()
        }
        AppMenuItem {
            text: qsTr("Export")
            enabled: root.operationEnabled
            onTriggered: root.exportSelectionRequested()
        }
    }

    AppMenuSeparator {
        visible: root.insideSelection
    }

    AppMenuItem {
        text: qsTr("Paste")
        enabled: !root.inputBlocked && !root.importActive
        onTriggered: root.pasteRequested()
    }

    AppMenuItem {
        text: qsTr("Import")
        enabled: !root.inputBlocked && !root.importActive
        onTriggered: root.importRequested()
    }

    AppMenuSeparator {}

    AppMenu {
        title: qsTr("Copy info")

        AppMenuItem {
            text: qsTr("Copy block name") + " " + (root.blockInfo.blockName || "")
            onTriggered: root.copyTextRequested(root.blockInfo.blockName || "")
        }
        AppMenuItem {
            text: qsTr("Copy biome name") + " " + (root.blockInfo.biomeName || "")
            onTriggered: root.copyTextRequested(root.blockInfo.biomeName || "")
        }
        AppMenuItem {
            text: qsTr("Copy altitude") + " " + (root.blockInfo.altitude === undefined ? "" : root.blockInfo.altitude)
            onTriggered: {
                if (root.blockInfo.altitude !== undefined)
                    root.copyTextRequested(String(root.blockInfo.altitude))
            }
        }
        AppMenuItem {
            text: qsTr("Copy TP command") + " " + (root.blockInfo.tpCommand || "")
            onTriggered: root.copyTextRequested(root.blockInfo.tpCommand || "")
        }
    }

    AppMenuSeparator {}

    AppMenuItem {
        visible: root.insideSelection
        text: qsTr("Save selection screenshot")
        onTriggered: root.screenshotRequested()
    }
    AppMenuItem {
        visible: !root.insideSelection
        text: qsTr("Save screenshot")
        onTriggered: root.screenshotRequested()
    }

    AppMenuSeparator {}

    AppMenuItem {
        visible: root.insideSelection && root.selectionRects === 1
        text: qsTr("View in 3D")
        onTriggered: root.voxelViewRequested()
    }
    AppMenuItem {
        text: qsTr("Open chunk editor")
        onTriggered: root.chunkEditorRequested()
    }
}

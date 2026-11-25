import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import "controls"

Item {
    id: root
    property var controller: null
    property bool controlsVisible: true
    property var exportTarget: null
    property vector3d exportMinimum
    property vector3d exportMaximum
    property bool exportEntities: true
    property bool exportIncludeAir: true
    property bool exportIncludeStructureVoid: false
    property bool exportNewFormat: false
    property bool exportCompressMesh: false
    readonly property bool hasModel: !!controller && controller.hasVoxelModel
    readonly property bool blockVolumeEditingEnabled: !!controller && controller.blockVolumeEditingEnabled
    readonly property bool importPreviewActive: !!controller && controller.importPreviewActive
    readonly property bool busy: !!controller && controller.busy
    readonly property bool sectionsEnabled: root.hasModel && !root.controller.loading
    readonly property bool operationsEnabled: root.hasModel && !root.busy && !root.importPreviewActive
    readonly property bool textInputFocused: Window.activeFocusItem instanceof TextInput
                                             || Window.activeFocusItem instanceof TextEdit
    readonly property bool viewShortcutsEnabled: root.visible && root.sectionsEnabled && !root.textInputFocused
    readonly property var camera: viewport.camera

    function shortcutEnabled(command, requiresSelection) {
        if (!viewShortcutsEnabled)
            return false
        if (importPreviewActive && (command === "toggleSelection" || command === "toggleSelectionMove"))
            return false
        return !requiresSelection || camera.selectionEnabled
    }

    /// The selection when enabled, otherwise the whole model, in model-local coordinates.
    function exportRange() {
        return camera.selectionEnabled ? {
                                             "minimum": camera.selectionMinimum,
                                             "maximum": camera.selectionMaximum
                                         } : {
            "minimum": Qt.vector3d(0, 0, 0),
            "maximum": camera.modelSize
        }
    }

    function prepareExport() {
        if (!operationsEnabled || structureExportDialog.visible || glbExportDialog.visible)
            return false
        const range = exportRange()
        exportTarget = controller
        exportMinimum = range.minimum
        exportMaximum = range.maximum
        return true
    }

    function chooseStructureExport() {
        if (!prepareExport())
            return
        exportEntities = structureSection.includeEntities
        exportIncludeAir = structureSection.includeAir
        exportIncludeStructureVoid = structureSection.includeStructureVoid
        exportNewFormat = structureSection.newFormat
        const origin = camera.modelOrigin
        const fileName = "structure-" + [origin.x + exportMinimum.x, origin.y + exportMinimum.y, origin.z
                                         + exportMinimum.z].join("_") + ".mcstructure"
        structureExportDialog.selectedFile = FileDialogSupport.selectedUrl(structureExportDialog, fileName)
        structureExportDialog.open()
    }

    function chooseGlbExport() {
        if (!prepareExport())
            return
        exportCompressMesh = compressMesh.checked
        const origin = camera.modelOrigin
        const fileName = "voxel-" + [origin.x + exportMinimum.x, origin.y + exportMinimum.y, origin.z
                                     + exportMinimum.z].join("_") + ".glb"
        glbExportDialog.selectedFile = FileDialogSupport.selectedUrl(glbExportDialog, fileName)
        glbExportDialog.open()
    }

    function deleteSelection() {
        if (blockVolumeEditingEnabled && camera.selectionEnabled && operationsEnabled)
            controller.deleteBlockVolumeAsync(camera.selectionMinimum, camera.selectionMaximum,
                                              structureSection.includeEntities)
    }

    function chooseStructureImport() {
        if (blockVolumeEditingEnabled && operationsEnabled)
            structureImportDialog.open()
    }

    function confirmImport() {
        if (importPreviewActive && !controller.importing)
            controller.commitImport(camera.selectionMinimum, camera.selectionMaximum, structureSection.includeEntities,
                                    structureSection.replaceNonAirBlocks)
    }

    function copySelection() {
        if (!operationsEnabled)
            return
        const range = exportRange()
        if (!controller.copySelection(range.minimum, range.maximum, structureSection.includeEntities,
                                      structureSection.newFormat, structureSection.includeAir,
                                      structureSection.includeStructureVoid))
            App.notifications.error(qsTr("Copy failed"), "", qsTr("Could not copy the selected blocks."))
    }

    function pasteStructure() {
        if (blockVolumeEditingEnabled && operationsEnabled)
            controller.pasteStructure()
    }

    Connections {
        target: root.exportTarget
        function onExportFinished(path, error) {
            if (error.length > 0)
                App.notifications.error(qsTr("Export failed"), "", error)
            else
                App.notifications.info(qsTr("Export successful"))
        }
    }

    Connections {
        target: root.controller
        function onImportPreviewChanged() {
            if (root.importPreviewActive)
                root.camera.activateImportSelection(root.controller.importSize)
            else
                root.camera.deactivateImportSelection()
        }
        function onImportFailed(message) {
            App.notifications.error(qsTr("Import failed"), "", message)
        }
        function onImportFinished() {
            App.notifications.info(qsTr("Import successful"))
        }
        function onCopyFailed(message) {
            App.notifications.error(qsTr("Copy failed"), "", message)
        }
        function onCopyFinished() {
            App.notifications.info(qsTr("Structure copied."))
        }
        function onDeleteFailed(message) {
            App.notifications.error(qsTr("Delete failed"), "", message)
        }
    }

    function runViewShortcut(command) {
        switch (command) {
        case "resetCamera":
            camera.resetView()
            return
        case "faceFront":
            camera.focusFrontFace()
            return
        case "toggleOrthographic":
            camera.orthoMode = !camera.orthoMode
            return
        case "toggleAxes":
            viewport.showAxes = !viewport.showAxes
            return
        case "toggleRotationLock":
            camera.rotationLocked = !camera.rotationLocked
            return
        case "toggleSelection":
            camera.selectionEnabled = !camera.selectionEnabled
            return
        case "toggleSelectionMove":
            camera.selectionMoveMode = !camera.selectionMoveMode
            return
        case "rotateLeft":
            camera.rotateView(-90, 0)
            return
        case "rotateRight":
            camera.rotateView(90, 0)
            return
        case "rotateUp":
            camera.rotateView(0, -90)
            return
        case "rotateDown":
            camera.rotateView(0, 90)
            return
        }
    }

    // Shortcut needs an Item parent to find its window, hence the Item wrapper.
    Repeater {
        model: ShortcutRegistry.voxel
        delegate: Item {
            id: shortcutHost
            required property var modelData
            Shortcut {
                sequence: shortcutHost.modelData.sequence
                enabled: root.shortcutEnabled(shortcutHost.modelData.command, shortcutHost.modelData.requiresSelection)
                onActivated: root.runViewShortcut(shortcutHost.modelData.command)
            }
        }
    }

    FileUrlDialog {
        id: structureImportDialog
        objectName: "voxelStructureImportDialog"
        title: qsTr("Import structure")
        fileMode: FileDialog.OpenFile
        nameFilters: FileDialogSupport.mcstructureFilters
        onAccepted: {
            if (!root.controller || !root.controller.importStructure(selectedUrl))
                AppErrorReporter.report(qsTr("Import failed"), root.controller)
        }
    }

    FileUrlDialog {
        id: structureExportDialog
        objectName: "voxelStructureExportDialog"
        title: qsTr("Export structure")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mcstructure"
        nameFilters: FileDialogSupport.mcstructureFilters
        onAccepted: {
            if (!root.exportTarget || !root.exportTarget.exportMcstructure(selectedUrl, root.exportMinimum, root.exportMaximum,
                                                                           root.exportEntities, root.exportNewFormat,
                                                                           root.exportIncludeAir, root.exportIncludeStructureVoid))
                AppErrorReporter.report(qsTr("Export failed"), root.exportTarget)
        }
    }
    FileUrlDialog {
        id: glbExportDialog
        objectName: "voxelGlbExportDialog"
        title: qsTr("Export GLB")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "glb"
        nameFilters: FileDialogSupport.glbFilters
        onAccepted: {
            if (!root.exportTarget || !root.exportTarget.exportGlb(selectedUrl, root.exportMinimum, root.exportMaximum,
                                                                   root.exportCompressMesh))
                AppErrorReporter.report(qsTr("Export failed"), root.exportTarget)
        }
    }

    SplitView {
        id: previewSplitView
        anchors.fill: parent
        orientation: Qt.Vertical

        handle: AppSplitHandle {
            orientation: previewSplitView.orientation
            visible: root.controlsVisible
        }

        Item {
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            SplitView.minimumHeight: Math.min(Theme.ui(160), root.height * 0.5)

            VoxelView {
                id: viewport
                objectName: "voxelViewport"
                anchors.fill: parent
                controller: root.controller
            }

            AppCard {
                id: controlsToolbar
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.gapLarge
                z: 1
                width: controlsToggle.width + Theme.gapSmall * 2
                height: controlsToggle.height + Theme.gapSmall * 2

                AppIconButton {
                    id: controlsToggle
                    objectName: "voxelControlsToggle"
                    anchors.centerIn: parent
                    iconName: root.controlsVisible ? "caret_down" : "caret_up"
                    label: root.controlsVisible ? qsTr("Hide controls panel") : qsTr("Show controls panel")
                    onClicked: root.controlsVisible = !root.controlsVisible
                }
            }
        }

        Flickable {
            id: controlsPanel
            objectName: "voxelControlsPanel"
            visible: root.controlsVisible
            SplitView.fillWidth: true
            SplitView.minimumHeight: Math.min(Theme.ui(120), root.height * 0.35)
            SplitView.preferredHeight: Math.min(root.height * 0.45, contentHeight)
            contentWidth: width
            contentHeight: sections.implicitHeight + Theme.gapSmall * 2
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: AppScrollBar {}
            ColumnLayout {
                id: sections
                x: Theme.gapSmall
                y: Theme.gapSmall
                width: Math.max(0, controlsPanel.width - Theme.gapSmall * 2)
                spacing: Theme.gapSmall

                VoxelSelectionSection {
                    camera: root.camera
                    importPreviewActive: root.importPreviewActive
                    enabled: root.sectionsEnabled
                }

                VoxelStructureSection {
                    id: structureSection
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    controller: root.controller
                    editingEnabled: root.blockVolumeEditingEnabled
                    operationsEnabled: root.operationsEnabled
                    selectionEnabled: root.camera.selectionEnabled
                    onImportRequested: root.chooseStructureImport()
                    onExportRequested: root.chooseStructureExport()
                    onCopyRequested: root.copySelection()
                    onPasteRequested: root.pasteStructure()
                    onDeleteRequested: root.deleteSelection()
                    onConfirmImportRequested: root.confirmImport()
                }

                VoxelPreviewSection {
                    objectName: "voxelModelExportSection"
                    title: qsTr("Model")
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    enabled: root.sectionsEnabled
                    contentItem: RowLayout {
                        spacing: Theme.gapSmall

                        AppCheckBox {
                            id: compressMesh
                            objectName: "voxelCompressMesh"
                            Layout.fillWidth: true
                            text: qsTr("Compress mesh")
                            checked: false
                        }

                        AppButton {
                            objectName: "voxelExportGlb"
                            Layout.preferredWidth: implicitWidth
                            Layout.minimumWidth: 0
                            compact: true
                            text: root.controller && root.controller.exporting ? qsTr("Exporting…") : qsTr("Export GLB")
                            enabled: root.operationsEnabled
                            onClicked: root.chooseGlbExport()
                        }
                    }
                }
            }
        }
    }
}

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
    property bool exportNewFormat: false
    readonly property bool blockVolumeEditingEnabled: !!controller && controller.blockVolumeEditingEnabled
    readonly property bool importPreviewActive: !!controller && controller.importPreviewActive
    readonly property bool importing: !!controller && controller.importing
    readonly property bool copying: !!controller && controller.copying
    readonly property bool textInputFocused: Window.activeFocusItem instanceof TextInput
                                               || Window.activeFocusItem instanceof TextEdit
    readonly property bool viewShortcutsEnabled: root.visible && root.hasModel && !!root.controller && !root.controller.loading &&
                                                 !root.importPreviewActive && !root.textInputFocused

    function chooseStructureExport() {
        if (!controller || controller.loading || controller.exporting || !hasModel || structureExportDialog.visible
                || glbExportDialog.visible)
            return
        exportTarget = controller
        exportMinimum = viewport.camera.selectionEnabled ? viewport.camera.selectionMinimum : Qt.vector3d(0, 0, 0)
        exportMaximum = viewport.camera.selectionEnabled ? viewport.camera.selectionMaximum : viewport.camera.modelSize
        exportEntities = includeEntities.checked
        exportNewFormat = structureFormat.currentIndex === 1
        structureExportDialog.open()
    }

    function chooseGlbExport() {
        if (!controller || controller.loading || controller.exporting || !hasModel || structureExportDialog.visible
                || glbExportDialog.visible)
            return
        exportTarget = controller
        exportMinimum = viewport.camera.selectionEnabled ? viewport.camera.selectionMinimum : Qt.vector3d(0, 0, 0)
        exportMaximum = viewport.camera.selectionEnabled ? viewport.camera.selectionMaximum : viewport.camera.modelSize
        glbExportDialog.open()
    }

    function deleteSelection() {
        if (!blockVolumeEditingEnabled || !viewport.camera.selectionEnabled || !controller)
            return
        controller.deleteBlockVolumeAsync(viewport.camera.selectionMinimum, viewport.camera.selectionMaximum,
                                          includeEntities.checked)
    }

    function chooseStructureImport() {
        if (!blockVolumeEditingEnabled || !controller || controller.loading || controller.deleting || importing
                || copying)
            return
        structureImportDialog.open()
    }

    function confirmImport() {
        if (!importPreviewActive || importing || !controller)
            return
        controller.commitImport(viewport.camera.selectionMinimum, viewport.camera.selectionMaximum,
                                includeEntities.checked)
    }

    function copySelection() {
        if (!controller || copying)
            return
        const minimum = viewport.camera.selectionEnabled ? viewport.camera.selectionMinimum : Qt.vector3d(0, 0, 0)
        const maximum = viewport.camera.selectionEnabled ? viewport.camera.selectionMaximum : viewport.camera.modelSize
        controller.copySelection(minimum, maximum, includeEntities.checked, structureFormat.currentIndex === 1)
    }

    function pasteStructure() {
        if (!blockVolumeEditingEnabled || !controller || importing || copying)
            return
        controller.pasteStructure()
    }
    function resetCameraView() {
        viewport.camera.resetView()
    }
    function focusFrontFace() {
        viewport.camera.focusFrontFace()
    }
    function rotateView(yaw, pitch) {
        viewport.camera.rotateView(yaw, pitch)
    }
    function setOrthoMode(enabled) {
        viewport.camera.orthoMode = enabled
    }
    function toggleOrthoMode() {
        setOrthoMode(!viewport.camera.orthoMode)
    }
    function setAxesVisible(visible) {
        viewport.showAxes = visible
    }
    function toggleAxes() {
        setAxesVisible(!viewport.showAxes)
    }
    function setRotationLocked(locked) {
        viewport.camera.rotationLocked = locked
    }
    function toggleRotationLocked() {
        setRotationLocked(!viewport.camera.rotationLocked)
    }
    function setSelectionEnabled(enabled) {
        viewport.camera.selectionEnabled = enabled
    }
    function toggleSelection() {
        setSelectionEnabled(!viewport.camera.selectionEnabled)
    }
    function setSelectionMoveMode(enabled) {
        viewport.camera.selectionMoveMode = enabled
    }
    function toggleSelectionMoveMode() {
        setSelectionMoveMode(!viewport.camera.selectionMoveMode)
    }

    Connections {
        target: root.exportTarget
        function onExportFinished(path, error) {
            if (error.length > 0)
                App.notifications.error(qsTr("Export failed"))
            else
                App.notifications.info(qsTr("Export successful"))
        }
    }

    Connections {
        target: root.controller
        function onImportPreviewChanged() {
            if (root.importPreviewActive)
                viewport.camera.activateImportSelection(root.controller.importSize)
            else
                viewport.camera.deactivateImportSelection()
        }
        function onImportFailed(message) {
            App.notifications.error(qsTr("Import failed"))
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

    Shortcut {
        sequence: "R"
        enabled: root.viewShortcutsEnabled
        onActivated: root.resetCameraView()
    }
    Shortcut {
        sequence: "F"
        enabled: root.viewShortcutsEnabled
        onActivated: root.focusFrontFace()
    }
    Shortcut {
        sequence: "O"
        enabled: root.viewShortcutsEnabled
        onActivated: root.toggleOrthoMode()
    }
    Shortcut {
        sequence: "A"
        enabled: root.viewShortcutsEnabled
        onActivated: root.toggleAxes()
    }
    Shortcut {
        sequence: "L"
        enabled: root.viewShortcutsEnabled
        onActivated: root.toggleRotationLocked()
    }
    Shortcut {
        sequence: "S"
        enabled: root.viewShortcutsEnabled
        onActivated: root.toggleSelection()
    }
    Shortcut {
        sequence: "M"
        enabled: root.viewShortcutsEnabled && viewport.camera.selectionEnabled
        onActivated: root.toggleSelectionMoveMode()
    }
    Shortcut {
        sequence: "Left"
        enabled: root.viewShortcutsEnabled
        onActivated: root.rotateView(-90, 0)
    }
    Shortcut {
        sequence: "Right"
        enabled: root.viewShortcutsEnabled
        onActivated: root.rotateView(90, 0)
    }
    Shortcut {
        sequence: "Up"
        enabled: root.viewShortcutsEnabled
        onActivated: root.rotateView(0, -90)
    }
    Shortcut {
        sequence: "Down"
        enabled: root.viewShortcutsEnabled
        onActivated: root.rotateView(0, 90)
    }

    FileUrlDialog {
        id: structureImportDialog
        objectName: "voxelStructureImportDialog"
        title: qsTr("Import structure")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Minecraft structures (*.mcstructure)")]
        onAccepted: {
            if (!root.controller || !root.controller.importStructure(selectedUrl))
                App.notifications.error(qsTr("Import failed"))
        }
    }

    FileUrlDialog {
        id: structureExportDialog
        objectName: "voxelStructureExportDialog"
        title: qsTr("Export structure")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mcstructure"
        nameFilters: [qsTr("Minecraft structures (*.mcstructure)")]
        onAccepted: {
            if (!root.exportTarget || !root.exportTarget.exportMcstructure(selectedUrl, root.exportMinimum, root.exportMaximum,
                                                                           root.exportEntities, root.exportNewFormat))
                App.notifications.error(qsTr("Export failed"))
        }
    }
    FileUrlDialog {
        id: glbExportDialog
        objectName: "voxelGlbExportDialog"
        title: qsTr("Export GLB")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "glb"
        nameFilters: [qsTr("GLB models (*.glb)")]
        onAccepted: {
            if (!root.exportTarget || !root.exportTarget.exportGlb(selectedUrl, root.exportMinimum, root.exportMaximum))
                App.notifications.error(qsTr("Export failed"))
        }
    }
    readonly property bool hasModel: viewport.camera.modelSize.x > 0 && viewport.camera.modelSize.y > 0
                                     && viewport.camera.modelSize.z > 0
    readonly property var worldOrigin: [viewport.camera.modelOrigin.x, viewport.camera.modelOrigin.y,
        viewport.camera.modelOrigin.z]

    function axisValue(vector, axis) {
        return axis === 0 ? vector.x : axis === 1 ? vector.y : vector.z
    }

    component SelectionCoordinate: AppSpinBox {
        required property int axis
        property bool upperBoundary: false
        readonly property int origin: root.worldOrigin[axis]
        readonly property int lower: root.axisValue(viewport.camera.selectionMinimum, axis)
        readonly property int upper: root.axisValue(viewport.camera.selectionMaximum, axis)
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 110
        from: origin + (upperBoundary ? lower + 1 : 0)
        to: Math.max(from, origin + (upperBoundary ? root.axisValue(viewport.camera.modelSize, axis) : upper - 1))
        value: Math.max(from, Math.min(to, origin + (upperBoundary ? upper : lower)))
        onValueModified: viewport.camera.setSelectionBoundary(axis, upperBoundary, value - origin)
        Accessible.name: (upperBoundary ? qsTr("Maximum (exclusive)") : qsTr("Minimum")) + " " + ["X", "Y", "Z"][axis]
    }

    component PreviewCheckBox: AppCheckBox {
        id: toggle
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: Math.max(Theme.controlHeight, implicitContentHeight + topPadding + bottomPadding)
        contentItem: Text {
            leftPadding: toggle.indicator.width + toggle.spacing
            text: toggle.text
            color: toggle.enabled ? Theme.text : Theme.muted
            font: toggle.font
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }

    component PreviewSection: GroupBox {
        id: previewSection
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.alignment: Qt.AlignTop
        padding: Theme.gapSmall
        topPadding: padding + implicitLabelHeight + spacing
        spacing: Theme.gapSmall
        label: Text {
            x: previewSection.leftPadding
            y: previewSection.padding
            text: previewSection.title
            color: Theme.text
            font.pixelSize: Theme.fontLabel
        }
        background: AppCard {}
    }

    component ViewButton: AppIconButton {
        buttonSize: Theme.iconButtonSmallSize
        Layout.preferredWidth: controlSize
        Layout.preferredHeight: controlSize
    }

    component OperationButton: AppButton {
        compact: true
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 0
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Math.min(160, root.height * 0.5)

            VoxelView {
                id: viewport
                objectName: "voxelViewport"
                anchors.fill: parent
                controller: root.controller
            }

            ToolButton {
                id: controlsToggle
                objectName: "voxelControlsToggle"
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.margins: Theme.gapSmall
                z: 1
                implicitWidth: Math.max(36, Theme.touch ? Theme.controlHeight : 0)
                implicitHeight: implicitWidth
                text: root.controlsVisible ? "▼" : "▲"
                Accessible.name: root.controlsVisible ? qsTr("Hide controls panel") : qsTr("Show controls panel")
                AppToolTip {
                    text: controlsToggle.Accessible.name
                    shown: controlsToggle.hovered
                }
                onClicked: root.controlsVisible = !root.controlsVisible
                contentItem: Text {
                    text: controlsToggle.text
                    color: Theme.text
                    font.pixelSize: Theme.fontLabel
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.radiusControl
                    color: controlsToggle.down ? Theme.accentSoft : (controlsToggle.hovered ? Theme.surfaceAlt :
                                                                                              Theme.surface)

                    border.width: Theme.borderWidth
                    border.color: Theme.border
                    opacity: 0.95
                }
            }
        }

        Rectangle {
            visible: root.controlsVisible
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        Flickable {
            id: controlsPanel
            objectName: "voxelControlsPanel"
            visible: root.controlsVisible
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(root.height * 0.45, contentHeight)
            contentWidth: width
            contentHeight: sections.implicitHeight + Theme.gapSmall * 2
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: ScrollBar {}
            GridLayout {
                id: sections
                x: Theme.gapSmall
                y: Theme.gapSmall
                width: Math.max(0, controlsPanel.width - Theme.gapSmall * 2)
                columns: 1
                uniformCellWidths: true
                columnSpacing: Theme.gapSmall
                rowSpacing: Theme.gapSmall

                PreviewSection {
                    objectName: "voxelSelectionSection"
                    title: qsTr("Selection")
                    enabled: root.hasModel && !!root.controller && !root.controller.loading
                    contentItem: ColumnLayout {
                        spacing: Theme.gapSmall

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: Theme.gapSmall

                            PreviewCheckBox {
                                objectName: "voxelSelectionToggle"
                                text: qsTr("Enable selection")
                                enabled: !root.importPreviewActive
                                checked: viewport.camera.selectionEnabled
                                onToggled: root.setSelectionEnabled(checked)
                            }
                            PreviewCheckBox {
                                objectName: "voxelSelectionMoveToggle"
                                text: qsTr("Move selection")
                                enabled: viewport.camera.selectionEnabled && !root.importPreviewActive
                                checked: viewport.camera.selectionMoveMode
                                onToggled: root.setSelectionMoveMode(checked)
                            }
                        }

                        GridLayout {
                            objectName: "voxelSelectionCoordinates"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            enabled: viewport.camera.selectionEnabled && !root.importPreviewActive
                            columns: 4
                            columnSpacing: Theme.gapSmall
                            rowSpacing: Theme.gapSmall

                            Item {}
                            Repeater {
                                model: ["X", "Y", "Z"]
                                delegate: Text {
                                    required property string modelData
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    Layout.preferredWidth: 110
                                    text: modelData
                                    color: Theme.muted
                                    font.pixelSize: Theme.fontCaption
                                    horizontalAlignment: Text.AlignHCenter
                                }
                            }

                            Text {
                                text: qsTr("Start")
                                color: Theme.muted
                                font.pixelSize: Theme.fontCaption
                            }
                            Repeater {
                                model: 3
                                delegate: SelectionCoordinate {
                                    required property int index
                                    axis: index
                                    objectName: "voxelSelectionMin" + ["X", "Y", "Z"][axis]
                                }
                            }

                            Text {
                                text: qsTr("End")
                                color: Theme.muted
                                font.pixelSize: Theme.fontCaption
                            }
                            Repeater {
                                model: 3
                                delegate: SelectionCoordinate {
                                    required property int index
                                    axis: index
                                    upperBoundary: true
                                    objectName: "voxelSelectionMax" + ["X", "Y", "Z"][axis]
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: Theme.gapSmall

                    PreviewSection {
                        objectName: "voxelViewSection"
                        title: qsTr("View")
                        Layout.fillHeight: true
                        enabled: root.hasModel && !!root.controller && !root.controller.loading
                        contentItem: RowLayout {
                            objectName: "voxelViewControls"
                            spacing: Theme.gapMedium

                            ColumnLayout {
                                objectName: "voxelViewToggles"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: Theme.gapSmall

                                PreviewCheckBox {
                                    objectName: "voxelOrthoToggle"
                                    text: qsTr("Orthographic projection")
                                    checked: viewport.camera.orthoMode
                                    onToggled: root.setOrthoMode(checked)
                                }
                                PreviewCheckBox {
                                    objectName: "voxelRotationLockToggle"
                                    text: qsTr("Lock rotation")
                                    checked: viewport.camera.rotationLocked
                                    onToggled: root.setRotationLocked(checked)
                                }
                                PreviewCheckBox {
                                    objectName: "voxelShowAxesToggle"
                                    text: qsTr("Show axes")
                                    checked: viewport.showAxes
                                    onToggled: root.setAxesVisible(checked)
                                }
                            }

                            GridLayout {
                                objectName: "voxelViewDirectionPad"
                                Layout.alignment: Qt.AlignVCenter
                                columns: 3
                                columnSpacing: Theme.gapSmall
                                rowSpacing: 0

                                Item {}
                                ViewButton {
                                    objectName: "voxelRotateUp"
                                    iconName: "caret_up"
                                    label: qsTr("Rotate up")
                                    onClicked: root.rotateView(0, -90)
                                }
                                Item {}

                                ViewButton {
                                    objectName: "voxelRotateLeft"
                                    iconName: "caret_left"
                                    label: qsTr("Rotate left")
                                    onClicked: root.rotateView(-90, 0)
                                }
                                ViewButton {
                                    objectName: "voxelFaceFront"
                                    iconName: "front"
                                    label: qsTr("Face front")
                                    onClicked: root.focusFrontFace()
                                }
                                ViewButton {
                                    objectName: "voxelRotateRight"
                                    iconName: "caret_right"
                                    label: qsTr("Rotate right")
                                    onClicked: root.rotateView(90, 0)
                                }

                                Item {}
                                ViewButton {
                                    objectName: "voxelRotateDown"
                                    iconName: "caret_down"
                                    label: qsTr("Rotate down")
                                    onClicked: root.rotateView(0, 90)
                                }
                                Item {}
                            }
                        }
                    }

                    PreviewSection {
                        objectName: "voxelModelExportSection"
                        title: qsTr("Model")
                        Layout.fillWidth: false
                        Layout.fillHeight: true
                        Layout.minimumWidth: implicitWidth
                        enabled: root.hasModel && !!root.controller && !root.controller.loading
                        contentItem: ColumnLayout {
                            spacing: Theme.gapSmall

                            AppCheckBox {
                                id: compressMesh
                                objectName: "voxelCompressMesh"
                                Layout.fillWidth: true
                                text: qsTr("Compress mesh")
                                checked: false
                            }

                            Item {
                                Layout.fillHeight: true
                            }

                            AppButton {
                                objectName: "voxelExportGlb"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                compact: true
                                text: root.controller && root.controller.exporting ? qsTr("Exporting…") : qsTr(
                                                                                         "Export GLB")
                                enabled: root.hasModel && !!root.controller && !root.controller.loading &&
                                         !root.controller.exporting && !root.controller.deleting &&
                                         !root.importPreviewActive && !root.importing && !root.copying
                                onClicked: root.chooseGlbExport()
                            }
                        }
                    }
                }

                PreviewSection {
                    objectName: "voxelOperationsSection"
                    title: qsTr("Structure")
                    Layout.columnSpan: sections.columns
                    contentItem: ColumnLayout {
                        spacing: Theme.gapSmall

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: Theme.gapSmall

                            AppCheckBox {
                                id: includeEntities
                                objectName: "voxelExportEntities"
                                text: qsTr("Include entities")
                                checked: true
                                Accessible.description: qsTr("Applies to structure and clipboard operations.")
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            AppComboBox {
                                id: structureFormat
                                objectName: "voxelExportFormat"
                                Layout.preferredWidth: Theme.ui(Theme.touch ? 112 : 130)
                                Layout.minimumWidth: 96
                                model: [qsTr("Old format"), qsTr("New format")]
                                Accessible.name: qsTr("Structure format")
                            }
                        }

                        RowLayout {
                            objectName: "voxelOperationActions"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: Theme.gapSmall

                            OperationButton {
                                objectName: "voxelImportStructure"
                                text: root.importing ? qsTr("Importing…") : qsTr("Import")
                                visible: root.blockVolumeEditingEnabled
                                enabled: root.hasModel && !!root.controller && !root.controller.loading &&
                                         !root.controller.exporting && !root.controller.deleting &&
                                         !root.importPreviewActive && !root.importing && !root.copying
                                onClicked: root.chooseStructureImport()
                            }
                            OperationButton {
                                objectName: "voxelExportStructure"
                                text: root.controller && root.controller.exporting ? qsTr("Exporting…") : qsTr("Export")
                                enabled: root.hasModel && !!root.controller && !root.controller.loading &&
                                         !root.controller.exporting && !root.controller.deleting &&
                                         !root.importPreviewActive && !root.importing && !root.copying
                                onClicked: root.chooseStructureExport()
                            }
                            OperationButton {
                                objectName: "voxelCopy"
                                text: root.copying ? qsTr("Copying…") : qsTr("Copy")
                                enabled: root.hasModel && !!root.controller && !root.controller.loading &&
                                         !root.controller.exporting && !root.controller.deleting && !root.importing &&
                                         !root.copying && !root.importPreviewActive
                                onClicked: root.copySelection()
                            }
                            OperationButton {
                                objectName: "voxelPaste"
                                text: qsTr("Paste")
                                visible: root.blockVolumeEditingEnabled
                                enabled: root.hasModel && !!root.controller && !root.controller.loading &&
                                         !root.controller.exporting && !root.controller.deleting && !root.importing &&
                                         !root.copying && !root.importPreviewActive
                                onClicked: root.pasteStructure()
                            }
                            OperationButton {
                                objectName: "voxelDelete"
                                text: root.controller && root.controller.deleting ? qsTr("Deleting…") : qsTr("Delete")
                                visible: root.blockVolumeEditingEnabled
                                enabled: root.hasModel && !!root.controller && !root.controller.loading &&
                                         !root.controller.exporting && !root.controller.deleting
                                         && viewport.camera.selectionEnabled && !root.importing && !root.copying &&
                                         !root.importPreviewActive
                                onClicked: root.deleteSelection()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            visible: root.importPreviewActive
                            spacing: Theme.gapSmall

                            Item {
                                Layout.fillWidth: true
                            }

                            AppButton {
                                compact: true
                                text: qsTr("Cancel")
                                enabled: !root.importing
                                onClicked: root.controller.cancelImport()
                            }

                            AppButton {
                                compact: true
                                text: root.importing ? qsTr("Importing…") : qsTr("Confirm import")
                                primary: true
                                enabled: !root.importing && viewport.camera.selectionEnabled
                                onClicked: root.confirmImport()
                            }
                        }
                    }
                }
            }
        }
    }
}

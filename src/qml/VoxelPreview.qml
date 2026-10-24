import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
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
    property bool exportIsGlb: false

    function chooseStructureExport() {
        if (!controller || controller.loading || controller.exporting || !hasModel
                || structureExportDialog.visible || glbExportDialog.visible)
            return
        exportIsGlb = false
        exportTarget = controller
        exportMinimum = viewport.camera.selectionEnabled ? viewport.camera.selectionMinimum : Qt.vector3d(0, 0, 0)
        exportMaximum = viewport.camera.selectionEnabled ? viewport.camera.selectionMaximum : viewport.camera.modelSize
        exportEntities = includeEntities.checked
        exportNewFormat = structureFormat.currentIndex === 1
        structureExportDialog.open()
    }

    function chooseGlbExport() {
        if (!controller || controller.loading || controller.exporting || !hasModel
                || structureExportDialog.visible || glbExportDialog.visible)
            return
        exportIsGlb = true
        exportTarget = controller
        exportMinimum = viewport.camera.selectionEnabled ? viewport.camera.selectionMinimum : Qt.vector3d(0, 0, 0)
        exportMaximum = viewport.camera.selectionEnabled ? viewport.camera.selectionMaximum : viewport.camera.modelSize
        glbExportDialog.open()
    }

    Connections {
        target: root.exportTarget
        function onExportFinished(path, error) {
            if (error.length > 0)
                Notifications.error(error, "", path)
            else
                Notifications.info(root.exportIsGlb ? qsTr("GLB exported.") : qsTr("Structure exported."), "", path)
        }
    }

    FileDialog {
        id: structureExportDialog
        objectName: "voxelStructureExportDialog"
        title: qsTr("Export structure")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mcstructure"
        nameFilters: [qsTr("Minecraft structures (*.mcstructure)")]
        onAccepted: {
            if (!root.exportTarget || !root.exportTarget.exportMcstructure(String(selectedFile), root.exportMinimum,
                    root.exportMaximum, root.exportEntities, root.exportNewFormat))
                Notifications.error(qsTr("The structure is not ready for export."))
        }
    }
    FileDialog {
        id: glbExportDialog
        objectName: "voxelGlbExportDialog"
        title: qsTr("Export GLB")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "glb"
        nameFilters: [qsTr("GLB models (*.glb)")]
        onAccepted: {
            if (!root.exportTarget || !root.exportTarget.exportGlb(String(selectedFile), root.exportMinimum, root.exportMaximum))
                Notifications.error(qsTr("The model is not ready for export."))
        }
    }
    readonly property bool hasModel: viewport.camera.modelSize.x > 0
                                    && viewport.camera.modelSize.y > 0 && viewport.camera.modelSize.z > 0
    readonly property var worldOrigin: [viewport.camera.modelOrigin.x,
                                       viewport.camera.modelOrigin.y,
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

    component OperationButton: AppButton {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        onClicked: developmentDialog.open()
    }

    component PreviewSection: Pane {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.alignment: Qt.AlignTop
        padding: Theme.gapMedium
        background: AppCard {}
    }

    component ViewButton: AppIconButton {
        buttonSize: 32
        Layout.preferredWidth: controlSize
        Layout.preferredHeight: controlSize
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
                implicitWidth: Math.max(36, Theme.touch ? Theme.touchTarget : 0)
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
                    color: controlsToggle.down ? Theme.accentSoft : (controlsToggle.hovered ? Theme.surfaceAlt : Theme.surface)
                    border.width: 1
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
                columns: width >= 640 * Theme.metricScale ? 2 : 1
                uniformCellWidths: true
                columnSpacing: Theme.gapSmall
                rowSpacing: Theme.gapSmall

                PreviewSection {
                    objectName: "voxelSelectionSection"
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
                                checked: viewport.camera.selectionEnabled
                                onToggled: viewport.camera.selectionEnabled = checked
                            }
                            PreviewCheckBox {
                                objectName: "voxelSelectionMoveToggle"
                                text: qsTr("Move selection")
                                enabled: viewport.camera.selectionEnabled
                                checked: viewport.camera.selectionMoveMode
                                onToggled: viewport.camera.selectionMoveMode = checked
                            }
                        }

                        GridLayout {
                            objectName: "voxelSelectionCoordinates"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            enabled: viewport.camera.selectionEnabled
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

                PreviewSection {
                    objectName: "voxelViewSection"
                    enabled: root.hasModel && !!root.controller && !root.controller.loading
                    contentItem: RowLayout {
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
                                onToggled: viewport.camera.orthoMode = checked
                            }
                            PreviewCheckBox {
                                objectName: "voxelRotationLockToggle"
                                text: qsTr("Lock rotation")
                                checked: viewport.camera.rotationLocked
                                onToggled: viewport.camera.rotationLocked = checked
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
                                icon: "▲"
                                label: qsTr("Rotate up")
                                onClicked: viewport.camera.rotateView(0, -90)
                            }
                            Item {}
                            ViewButton {
                                objectName: "voxelRotateLeft"
                                icon: "◀"
                                label: qsTr("Rotate left")
                                onClicked: viewport.camera.rotateView(-90, 0)
                            }
                            ViewButton {
                                objectName: "voxelFaceFront"
                                icon: "⊙"
                                label: qsTr("Face front")
                                onClicked: viewport.camera.focusFrontFace()
                            }
                            ViewButton {
                                objectName: "voxelRotateRight"
                                icon: "▶"
                                label: qsTr("Rotate right")
                                onClicked: viewport.camera.rotateView(90, 0)
                            }
                            Item {}
                            ViewButton {
                                objectName: "voxelRotateDown"
                                icon: "▼"
                                label: qsTr("Rotate down")
                                onClicked: viewport.camera.rotateView(0, 90)
                            }
                            Item {}
                        }
                    }
                }

                PreviewSection {
                    objectName: "voxelOperationsSection"
                    Layout.columnSpan: sections.columns
                    contentItem: ColumnLayout {
                        spacing: Theme.gapSmall

                        // Keep export settings on one row; narrow screens can scroll it horizontally.
                        Flickable {
                            id: structureScroller
                            objectName: "voxelStructureScroller"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredHeight: structureRow.implicitHeight
                            contentWidth: Math.max(width, structureRow.implicitWidth)
                            contentHeight: height
                            flickableDirection: Flickable.HorizontalFlick
                            boundsBehavior: Flickable.StopAtBounds
                            clip: true
                            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                            RowLayout {
                                id: structureRow
                                objectName: "voxelStructureOperations"
                                width: structureScroller.contentWidth
                                height: structureScroller.height
                                spacing: Theme.gapSmall

                                OperationButton {
                                    objectName: "voxelImportStructure"
                                    Layout.minimumWidth: implicitWidth
                                    text: qsTr("Import structure")
                                }
                                Rectangle {
                                    Layout.fillHeight: true
                                    implicitWidth: 1
                                    color: Theme.border
                                }
                                AppButton {
                                    objectName: "voxelExportStructure"
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: implicitWidth
                                    text: root.controller && root.controller.exporting ? qsTr("Exporting…") : qsTr("Export structure")
                                    enabled: root.hasModel && !!root.controller && !root.controller.loading && !root.controller.exporting
                                    onClicked: root.chooseStructureExport()
                                }
                                AppCheckBox {
                                    id: includeEntities
                                    objectName: "voxelExportEntities"
                                    text: qsTr("Export entities")
                                    checked: true
                                }
                                AppComboBox {
                                    id: structureFormat
                                    objectName: "voxelExportFormat"
                                    Layout.preferredWidth: 100
                                    Layout.minimumWidth: 100
                                    model: [qsTr("Old format"), qsTr("New format")]
                                    Accessible.name: qsTr("Structure format")
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: Theme.border
                        }

                        RowLayout {
                            objectName: "voxelClipboardOperations"
                            Layout.fillWidth: true
                            spacing: Theme.gapSmall
                            OperationButton {
                                objectName: "voxelCopy"
                                text: qsTr("Copy")
                            }
                            OperationButton {
                                objectName: "voxelPaste"
                                text: qsTr("Paste")
                            }
                            OperationButton {
                                objectName: "voxelCut"
                                text: qsTr("Cut")
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: Theme.border
                        }

                        AppButton {
                            objectName: "voxelExportGlb"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: root.controller && root.controller.exporting ? qsTr("Exporting…") : qsTr("Export GLB")
                            enabled: root.hasModel && !!root.controller && !root.controller.loading && !root.controller.exporting
                            onClicked: root.chooseGlbExport()
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: developmentDialog
        objectName: "voxelDevelopmentDialog"
        parent: Overlay.overlay
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width: Math.min(320, parent ? Math.max(0, parent.width - 32) : 320)
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        padding: Theme.cardPadding
        background: AppCard {}
        contentItem: ColumnLayout {
            spacing: Theme.gapMedium
            Text {
                Layout.fillWidth: true
                text: qsTr("Under development")
                color: Theme.text
                font.pixelSize: Theme.fontSubtitle
                wrapMode: Text.Wrap
            }
            AppButton {
                objectName: "voxelDevelopmentClose"
                Layout.alignment: Qt.AlignRight
                text: qsTr("OK")
                primary: true
                onClicked: developmentDialog.close()
            }
        }
    }

}

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import BedrockMap 1.0
import "controls"

Item {
    id: canvas
    // Scene Graph children must stay inside the map viewport. Without this,
    // terrain geometry can paint over the sidebar because QQuickItem does not
    // clip descendants by default.
    clip: true
    property var controller
    readonly property var regionImport: controller ? controller.regionImport : null
    readonly property bool importActive: !!regionImport && regionImport.active
    readonly property bool inputBlocked: !controller || controller.operationRunning || (!!regionImport
                                                                                        && regionImport.loading)
    property var terrainData
    property var globalData
    property bool showGrid: true
    property bool showCoordinates: false
    property bool showEntities: false
    property bool showSlimeChunks: false
    property bool showVillages: false
    property bool showHsa: false
    property bool showRenderDebug: true
    property bool selectionToolActive: false
    property real inputRightInset: 0
    property string operationText: ""
    property real panInertia: Settings ? Settings.panInertia : 0.35
    property point panVelocity: Qt.point(0, 0)
    property real lastPanSampleMs: 0
    property real lastPanMotionMs: 0
    property real lastInertiaTickMs: 0
    signal chunkEditorRequested(int chunkX, int chunkZ, int dimension)
    signal voxelViewRequested(int chunkX, int chunkZ, int dimension)
    /// Both debug panels use the bundled monospace face so numbers line up.
    readonly property font debugFont: Qt.font({
                                                  family: "JetBrains Mono",
                                                  pointSize: 8
                                              })

    FileDialog {
        id: importDialog
        title: qsTr("Import chunks")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("BedrockMap regions (*.bchks)")]
        property int targetX: 0
        property int targetZ: 0
        property int dimension: 0
        property var owner: null
        onAccepted: {
            if (owner && owner === canvas.controller && owner.dimension === dimension)
                owner.beginImportAsync(selectedFile, targetX, targetZ)
            owner = null
        }
        onRejected: owner = null
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Export selected chunks")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "bchks"
        nameFilters: [qsTr("BedrockMap regions (*.bchks)")]
        onAccepted: if (canvas.controller && canvas.controller.exportSelectionAsync(selectedFile))
                        canvas.operationText = qsTr("Exporting selected chunks…")
    }

    Dialog {
        id: biomeDialog
        parent: Overlay.overlay
        modal: true
        width: Math.min(420, parent ? parent.width - 32 : 420)
        height: 196
        padding: 16
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        property var options: []

        background: Rectangle {
            radius: Theme.radiusCard
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
        }
        contentItem: Column {
            spacing: 12
            Text {
                width: parent.width
                text: qsTr("Set biome")
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.bold: true
            }
            AppComboBox {
                id: biomeCombo
                width: parent.width
                model: biomeDialog.options
                textRole: "name"
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                AppButton {
                    text: qsTr("Cancel")
                    onClicked: biomeDialog.close()
                }
                AppButton {
                    text: qsTr("Apply")
                    primary: true
                    onClicked: canvas.applySelectedBiome()
                }
            }
        }
    }

    AppMenu {
        id: debugCopyMenu
        property string snapshot: ""
        AppMenuItem {
            text: "Copy"
            onTriggered: terrainLayer.copyDebugText(debugCopyMenu.snapshot)
        }
        AppMenuItem {
            text: "Reset timings"
            onTriggered: terrainLayer.resetTimings()
        }
    }

    AppMenu {
        id: mapContextMenu
        property bool insideSelection: false
        property int selectionRects: 0
        property int dimension: 0
        property var blockInfo: ({})

        AppMenuItem {
            visible: mapContextMenu.insideSelection
            text: qsTr("Unselect")
            onTriggered: canvas.controller.clearSelection()
        }
        AppMenu {
            title: qsTr("Selection operations")
            menuItemVisible: mapContextMenu.insideSelection
            AppMenuItem {
                text: qsTr("Delete")
                enabled: canvas.controller && !canvas.controller.operationRunning
                onTriggered: if (canvas.controller.deleteSelectionAsync())
                                 canvas.operationText = qsTr("Deleting selected chunks…")
            }
            AppMenuItem {
                text: qsTr("Create void")
                enabled: canvas.controller && !canvas.controller.operationRunning
                onTriggered: if (canvas.controller.createVoidSelectionAsync())
                                 canvas.operationText = qsTr("Clearing selected chunks…")
            }
            AppMenuItem {
                text: qsTr("Set biome")
                enabled: canvas.controller && !canvas.controller.operationRunning
                onTriggered: canvas.openBiomePicker()
            }
            AppMenuItem {
                text: qsTr("Copy")
                enabled: canvas.controller && !canvas.controller.operationRunning
                onTriggered: if (canvas.controller.copySelectionAsync())
                                 canvas.operationText = qsTr("Copying selected chunks…")
            }
            AppMenuItem {
                text: qsTr("Export")
                enabled: canvas.controller && !canvas.controller.operationRunning
                onTriggered: exportDialog.open()
            }
        }
        AppMenuSeparator {
            visible: mapContextMenu.insideSelection
        }
        AppMenuItem {
            text: qsTr("Paste")
            enabled: !canvas.inputBlocked && !canvas.importActive
            onTriggered: {
                const position = canvas.requestedChunkPosition()
                if (position && canvas.controller)
                    canvas.controller.beginPasteAsync(position.chunkX, position.chunkZ)
            }
        }
        AppMenuItem {
            text: qsTr("Import")
            enabled: !canvas.inputBlocked && !canvas.importActive
            onTriggered: {
                const position = canvas.requestedChunkPosition()
                if (!position)
                    return
                importDialog.targetX = position.chunkX
                importDialog.targetZ = position.chunkZ
                importDialog.dimension = position.dimension
                importDialog.owner = canvas.controller
                importDialog.open()
            }
        }
        AppMenuSeparator {}
        AppMenu {
            title: qsTr("Copy info")
            AppMenuItem {
                text: qsTr("Copy block name") + " " + (mapContextMenu.blockInfo.blockName || "")
                onTriggered: canvas.controller.copyText(mapContextMenu.blockInfo.blockName || "")
            }
            AppMenuItem {
                text: qsTr("Copy biome name") + " " + (mapContextMenu.blockInfo.biomeName || "")
                onTriggered: canvas.controller.copyText(mapContextMenu.blockInfo.biomeName || "")
            }
            AppMenuItem {
                text: qsTr("Copy altitude") + " " + (mapContextMenu.blockInfo.altitude === undefined ? "" :
                                                                                                       mapContextMenu.blockInfo.altitude)
                onTriggered: if (mapContextMenu.blockInfo.altitude !== undefined)
                                 canvas.controller.copyText(String(mapContextMenu.blockInfo.altitude))
            }
            AppMenuItem {
                text: qsTr("Copy TP command") + " " + (mapContextMenu.blockInfo.tpCommand || "")
                onTriggered: canvas.controller.copyText(mapContextMenu.blockInfo.tpCommand || "")
            }
        }
        AppMenuSeparator {}
        AppMenuItem {
            visible: mapContextMenu.insideSelection
            text: qsTr("Save selection screenshot")
        }
        AppMenuItem {
            visible: !mapContextMenu.insideSelection
            text: qsTr("Save screenshot")
        }
        AppMenuSeparator {}
        AppMenuItem {
            visible: mapContextMenu.insideSelection && mapContextMenu.selectionRects === 1
            text: qsTr("View in 3D")
            onTriggered: canvas.requestVoxelView()
        }
        AppMenuItem {
            text: qsTr("Open chunk editor")
            onTriggered: canvas.requestChunkEditor()
        }
    }

    Connections {
        target: canvas.controller
        function onOperationFinished() {
            canvas.operationText = ""
            Notifications.info(qsTr("Map operation completed"))
        }
        function onOperationFailed(message) {
            canvas.operationText = ""
            Notifications.error(qsTr("Map operation failed"), "", message)
        }
    }

    Connections {
        target: canvas.regionImport
        function onActiveChanged() {
            canvas.stopPanInertia()
            mapTouchInput.cancelGesture()
            if (canvas.importActive)
                canvas.selectionToolActive = false
        }
    }

    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: canvas.visible && canvas.importActive && !canvas.inputBlocked && !importXField.activeFocus &&
                 !importZField.activeFocus
        onActivated: canvas.confirmImport()
    }

    Shortcut {
        sequence: "Esc"
        enabled: canvas.visible && canvas.importActive && !canvas.inputBlocked
        onActivated: canvas.controller.cancelImport()
    }

    function confirmImport() {
        if (!controller || !importXField.acceptableInput || !importZField.acceptableInput)
            return
        const targetX = Number(importXField.text)
        const targetZ = Number(importZField.text)
        regionImport.targetX = targetX
        regionImport.targetZ = targetZ
        if (controller.confirmImport())
            operationText = qsTr("Importing chunks…")
    }

    // Zoom steps come from MapView::zoomToAdjacentLevel():
    // ZOOM_SUBDIVISIONS_PER_OCTAVE equal steps inside each
    // power of two, so a step is a fixed fraction of the current octave instead
    // of an arbitrary multiplier.
    function zoomIn(anchorX, anchorY) {
        if (!controller)
            return
        stopPanInertia()
        controller.zoomAdjacent(1, Qt.point(anchorX === undefined ? width / 2 : anchorX, anchorY === undefined ? height
                                                                                                                 / 2 : anchorY))
    }
    function zoomOut(anchorX, anchorY) {
        if (!controller)
            return
        stopPanInertia()
        controller.zoomAdjacent(-1, Qt.point(anchorX === undefined ? width / 2 : anchorX, anchorY === undefined
                                             ? height / 2 : anchorY))
    }
    function centerOnOrigin() {
        if (!controller)
            return
        stopPanInertia()
        controller.gotoBlock(0, 0)
    }
    function beginMapDrag() {
        terrainLayer.setDragActive(true)
    }
    function endMapDrag() {
        terrainLayer.setDragActive(false)
    }
    function panMapBy(dx, dy) {
        if (!controller || (dx === 0 && dy === 0))
            return
        controller.pan(dx, dy)
    }
    function stopPanInertia() {
        panInertiaTimer.stop()
        panVelocity = Qt.point(0, 0)
        lastPanSampleMs = 0
        lastPanMotionMs = 0
    }
    function beginPanGesture() {
        stopPanInertia()
        beginMapDrag()
        lastPanMotionMs = Date.now()
    }
    function updatePanGesture(dx, dy) {
        if (!controller || (dx === 0 && dy === 0))
            return
        panMapBy(dx, dy)

        const now = Date.now()
        if (lastPanSampleMs > 0) {
            const dt = Math.max(0.001, (now - lastPanSampleMs) / 1000)
            const sample = Qt.point(dx / dt, dy / dt)
            panVelocity = Qt.point(panVelocity.x * 0.55 + sample.x * 0.45, panVelocity.y * 0.55 + sample.y * 0.45)
        }
        lastPanSampleMs = now
        lastPanMotionMs = now
    }
    function endPanGesture() {
        endMapDrag()
        const now = Date.now()
        const strength = Math.max(0, Math.min(1, panInertia))
        const speed = Math.hypot(panVelocity.x, panVelocity.y)
        if (!controller || strength <= 0.001 || speed < 60 || now - lastPanMotionMs > 100) {
            stopPanInertia()
            return
        }

        const limitedSpeed = Math.min(speed, 4800)
        const scale = limitedSpeed / speed
        panVelocity = Qt.point(panVelocity.x * scale, panVelocity.y * scale)
        lastInertiaTickMs = now
        panInertiaTimer.start()
    }
    function stepPanInertia() {
        if (!controller) {
            stopPanInertia()
            return
        }
        const now = Date.now()
        const dt = Math.max(0.001, Math.min(0.05, (now - lastInertiaTickMs) / 1000))
        lastInertiaTickMs = now
        const strength = Math.max(0, Math.min(1, panInertia))
        const timeConstant = 0.08 + 0.52 * strength
        panMapBy(panVelocity.x * dt, panVelocity.y * dt)
        const decay = Math.exp(-dt / timeConstant)
        panVelocity = Qt.point(panVelocity.x * decay, panVelocity.y * decay)
        if (Math.hypot(panVelocity.x, panVelocity.y) < 45)
            stopPanInertia()
    }
    function openBiomePicker() {
        if (!controller)
            return
        biomeDialog.options = controller.biomeOptions()
        let selected = 0
        const biomeId = mapContextMenu.blockInfo.biomeId
        for (let index = 0; index < biomeDialog.options.length; ++index) {
            if (Number(biomeDialog.options[index].value) === Number(biomeId)) {
                selected = index
                break
            }
        }
        biomeCombo.currentIndex = selected
        biomeDialog.open()
    }
    function applySelectedBiome() {
        if (!controller || biomeCombo.currentIndex < 0 || biomeCombo.currentIndex >= biomeDialog.options.length)
            return
        const biome = biomeDialog.options[biomeCombo.currentIndex]
        if (controller.setSelectionBiomeAsync(biome.value))
            operationText = qsTr("Applying biome…")
        biomeDialog.close()
    }
    function openMapContextMenu(position) {
        if (inputBlocked || importActive)
            return
        stopPanInertia()
        mapContextMenu.insideSelection = controller.selectionContainsChunkAt(position)
        mapContextMenu.selectionRects = controller.selectionRectCount
        mapContextMenu.dimension = controller.dimension
        mapContextMenu.blockInfo = controller.blockInfoAt(position)
        const menuX = Math.max(0, Math.min(position.x, canvas.width - mapContextMenu.width))
        const menuY = Math.max(0, Math.min(position.y, canvas.height - mapContextMenu.height))
        mapContextMenu.popup(canvas, menuX, menuY)
    }
    function requestChunkEditor() {
        const position = requestedChunkPosition()
        if (position)
            chunkEditorRequested(position.chunkX, position.chunkZ, position.dimension)
    }
    function requestVoxelView() {
        const position = requestedChunkPosition()
        if (position)
            voxelViewRequested(position.chunkX, position.chunkZ, position.dimension)
    }
    function requestedChunkPosition() {
        const blockX = Number(mapContextMenu.blockInfo.blockX)
        const blockZ = Number(mapContextMenu.blockInfo.blockZ)
        if (!Number.isFinite(blockX) || !Number.isFinite(blockZ))
            return null
        return {
            chunkX: Math.floor(blockX / 16),
            chunkZ: Math.floor(blockZ / 16),
            dimension: mapContextMenu.dimension
        }
    }
    function renderDebugSnapshot() {
        return timingHeading.text + "\n" + rendererInfo.text + "\n" + timingZoom.text + "\n" + atlasState.text + "\n" + uploadState.text
                + "\n" + viewportState.text + "\n----------------------\n" + timingTree.text
    }

    onWidthChanged: if (controller)
                        controller.setViewportSize(Qt.size(width, height))
    onHeightChanged: if (controller)
                         controller.setViewportSize(Qt.size(width, height))
    Component.onCompleted: if (controller)
                               controller.setViewportSize(Qt.size(width, height))
    onControllerChanged: {
        stopPanInertia()
        if (mapTouchInput)
            mapTouchInput.cancelGesture()
        if (controller)
            controller.setViewportSize(Qt.size(width, height))
    }
    onInputRightInsetChanged: {
        stopPanInertia()
        if (mapTouchInput)
            mapTouchInput.cancelGesture()
    }
    onVisibleChanged: if (!visible) {
                          stopPanInertia()
                          mapTouchInput.cancelGesture()
                          if (controller)
                              controller.endImportDrag()
                      }
    onSelectionToolActiveChanged: {
        if (mapTouchInput)
            mapTouchInput.cancelGesture()
        if (selectionToolActive)
            stopPanInertia()
        else if (controller && controller.selectionDragging)
            controller.cancelSelectionDrag()
    }

    Timer {
        id: panInertiaTimer
        interval: 16
        repeat: true
        onTriggered: canvas.stepPanInertia()
    }

    Rectangle {
        anchors.fill: parent
        color: terrainLayer.hasData ? Theme.map : Theme.surfaceAlt
    }

    TerrainQuickItem {
        id: terrainLayer
        anchors.fill: parent
        z: 1
        controller: canvas.controller
        provider: canvas.terrainData
        metricsEnabled: canvas.showRenderDebug
        checkerColor: Theme.mapCheckerBase
        checkerAlternateColor: Theme.mapCheckerAlternate
    }

    Connections {
        target: Settings
        function onSaved() {
            terrainLayer.applySettings()
        }
    }

    // Keeps publishing the camera actually applied by the terrain node so the
    // coordinate labels stay in step with a held LOD frame.
    MapOverlayQuickItem {
        id: mapOverlay
        anchors.fill: parent
        z: 2
        controller: canvas.controller
        cameraSource: terrainLayer
        provider: canvas.terrainData
        globalData: canvas.globalData
        entitiesVisible: canvas.showEntities
        slimeChunksVisible: canvas.showSlimeChunks
        villagesVisible: canvas.showVillages
        hsaVisible: canvas.showHsa
        actorRenderStyle: Settings ? Settings.actorRenderStyle : 0
        gridVisible: canvas.showGrid
        gridColor: Settings ? Settings.gridLineColor : "#bbbbbb"
        selectionColor: Settings ? Settings.mapSelectionColor : "#50dafffb"
    }

    MapCoordinateLabels {
        anchors.fill: parent
        z: 3
        centerX: mapOverlay.viewCenterX
        centerZ: mapOverlay.viewCenterZ
        pixelsPerChunk: mapOverlay.viewScale
        labelsVisible: canvas.showCoordinates
    }

    AppBusyOverlay {
        anchors.fill: parent
        z: 20
        active: canvas.inputBlocked && !!canvas.controller
        text: canvas.regionImport && canvas.regionImport.loading ? qsTr("Loading region…") : canvas.operationText
    }

    AppCard {
        id: importPlacementPanel
        visible: canvas.importActive
        enabled: !canvas.inputBlocked
        z: 10
        x: Math.max(8, (canvas.width - canvas.inputRightInset - width) / 2)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        width: Math.max(0, Math.min(440, canvas.width - canvas.inputRightInset - 16))
        height: importPanelColumn.implicitHeight + 16

        // Reserve this entire strip from map input, including unused panel space.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: function (event) {
                event.accepted = true
            }
        }
        MultiPointTouchArea {
            anchors.fill: parent
            mouseEnabled: false
        }

        ColumnLayout {
            id: importPanelColumn
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6
            Text {
                Layout.fillWidth: true
                text: canvas.regionImport ? qsTr("%1 · %2 chunks · %3 × %4").arg(canvas.regionImport.sourceName).arg(
                                                canvas.regionImport.chunkCount).arg(canvas.regionImport.width).arg(
                                                canvas.regionImport.height) : ""
                elide: Text.ElideMiddle
                color: Theme.text
                font.pixelSize: Theme.fontCaption
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Drag inside to move; drag outside to pan. Two fingers move the map.")
                wrapMode: Text.Wrap
                color: Theme.muted
                font.pixelSize: Theme.fontCaption
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    text: "X"
                    color: Theme.text
                }
                AppTextField {
                    id: importXField
                    Layout.fillWidth: true
                    Layout.minimumWidth: 40
                    implicitWidth: 100
                    text: canvas.regionImport ? String(canvas.regionImport.targetX) : "0"
                    validator: IntValidator {}
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    onEditingFinished: {
                        if (canvas.regionImport && acceptableInput)
                            canvas.regionImport.targetX = Number(text)
                        syncCoordinate()
                    }
                    function syncCoordinate() {
                        text = canvas.regionImport ? String(canvas.regionImport.targetX) : "0"
                    }
                }
                Text {
                    text: "Z"
                    color: Theme.text
                }
                AppTextField {
                    id: importZField
                    Layout.fillWidth: true
                    Layout.minimumWidth: 40
                    implicitWidth: 100
                    text: canvas.regionImport ? String(canvas.regionImport.targetZ) : "0"
                    validator: IntValidator {}
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    onEditingFinished: {
                        if (canvas.regionImport && acceptableInput)
                            canvas.regionImport.targetZ = Number(text)
                        syncCoordinate()
                    }
                    function syncCoordinate() {
                        text = canvas.regionImport ? String(canvas.regionImport.targetZ) : "0"
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Target X/Z are chunk coordinates. Existing chunks will be overwritten.")
                wrapMode: Text.Wrap
                color: Theme.muted
                font.pixelSize: Theme.fontCaption
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                AppButton {
                    text: qsTr("Cancel")
                    onClicked: if (canvas.controller)
                                   canvas.controller.cancelImport()
                }
                AppButton {
                    text: qsTr("Import")
                    primary: true
                    enabled: importXField.acceptableInput && importZField.acceptableInput
                    onClicked: canvas.confirmImport()
                }
            }
        }
        Connections {
            target: canvas.regionImport
            function onPreviewChanged() {
                importXField.syncCoordinate()
                importZField.syncCoordinate()
            }
        }
    }

    Connections {
        target: canvas.terrainData
        function onWindowRefilled(blocksPerTexel) {
            mapOverlay.update()
        }
    }

    // Pointer handlers only forward camera gestures to MapController. Grid,
    // overlays and selection all use that single shared camera state.
    // Suspend mouse handlers during real touches, including synthesized mouse
    // events, so they cannot interrupt the touch area's exclusive grab.

    Item {
        id: mapInputRegion
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.bottomMargin: importPlacementPanel.visible ? importPlacementPanel.height + 16 : 0
        anchors.left: parent.left
        width: Math.max(0, canvas.width - canvas.inputRightInset)
        z: 4

        TapHandler {
            enabled: !canvas.inputBlocked && mapTouchInput.pointCount === 0
            acceptedDevices: PointerDevice.Mouse
            acceptedButtons: Qt.RightButton
            onTapped: function (eventPoint) {
                canvas.openMapContextMenu(eventPoint.position)
            }
        }

        TapHandler {
            enabled: !canvas.inputBlocked && mapTouchInput.pointCount === 0
            acceptedDevices: PointerDevice.Mouse
            acceptedButtons: Qt.LeftButton
            onPressedChanged: if (pressed)
                                  canvas.stopPanInertia()
            onLongPressed: canvas.openMapContextMenu(point.position)
        }

        DragHandler {
            id: mapDragHandler
            enabled: !canvas.inputBlocked && mapTouchInput.pointCount === 0
            target: null
            acceptedDevices: PointerDevice.Mouse
            acceptedButtons: Qt.LeftButton
            property point lastTranslation: Qt.point(0, 0)
            property bool movingImport: false
            function localPosition(scenePosition) {
                return canvas.mapFromItem(null, scenePosition.x, scenePosition.y)
            }
            onActiveChanged: {
                if (active) {
                    canvas.stopPanInertia()
                    movingImport = canvas.importActive && canvas.controller.beginImportDragAt(localPosition(
                                                                                                  centroid.scenePressPosition))
                    if (movingImport)
                        canvas.controller.updateImportDragAt(localPosition(centroid.scenePosition))
                    else
                        canvas.beginPanGesture()
                } else if (movingImport) {
                    if (canvas.controller)
                        canvas.controller.endImportDrag()
                    movingImport = false
                } else {
                    canvas.endPanGesture()
                }
                lastTranslation = Qt.point(0, 0)
            }
            onTranslationChanged: {
                if (!active || !canvas.controller)
                    return
                const panDelta = Qt.point(translation.x - lastTranslation.x, translation.y - lastTranslation.y)
                lastTranslation = translation
                if (movingImport)
                    canvas.controller.updateImportDragAt(localPosition(centroid.scenePosition))
                else
                    canvas.updatePanGesture(panDelta.x, panDelta.y)
            }
        }

        // Middle-button drag creates or edits the shared chunk selection.
        DragHandler {
            id: selectionDragHandler
            enabled: !canvas.inputBlocked && !canvas.importActive && mapTouchInput.pointCount === 0
            target: null
            acceptedDevices: PointerDevice.Mouse
            acceptedButtons: Qt.MiddleButton
            property bool selecting: false

            function localPosition() {
                return canvas.mapFromItem(null, centroid.scenePosition.x, centroid.scenePosition.y)
            }

            onActiveChanged: {
                if (!canvas.controller)
                    return
                if (active) {
                    canvas.stopPanInertia()
                    selecting = true
                    canvas.controller.beginSelectionDragAt(localPosition())
                } else if (selecting) {
                    selecting = false
                    canvas.controller.finishSelectionDrag()
                }
            }
            onTranslationChanged: {
                if (active && selecting && canvas.controller)
                    canvas.controller.updateSelectionDragAt(localPosition())
            }
        }

        AppTouchGestureArea {
            id: mapTouchInput
            anchors.fill: parent
            enabled: !canvas.inputBlocked
            longPressEnabled: !canvas.importActive
            property int activePoints: 0
            property bool selecting: false
            property bool movingImport: false
            property bool cameraOnlySequence: false

            onPointCountChanged: if (pointCount === 0)
                                     cameraOnlySequence = false
            onContactStarted: {
                canvas.stopPanInertia()
                if (pointCount >= 2)
                    cameraOnlySequence = true
            }
            onLongPressed: function (position) {
                canvas.openMapContextMenu(position)
            }
            onManipulationStarted: function (points, position) {
                activePoints = points
                movingImport = points === 1 && canvas.importActive && !cameraOnlySequence
                        && canvas.controller.beginImportDragAt(startPosition)
                selecting = points === 1 && !canvas.importActive && canvas.selectionToolActive && !cameraOnlySequence
                if (movingImport) {
                    canvas.stopPanInertia()
                } else if (selecting) {
                    canvas.beginMapDrag()
                    canvas.controller.beginSelectionDragAt(position)
                } else if (points === 1) {
                    canvas.beginPanGesture()
                } else {
                    canvas.beginMapDrag()
                }
            }
            onManipulationUpdated: function (points, previous, position, scale) {
                if (!canvas.controller)
                    return
                if (movingImport)
                    canvas.controller.updateImportDragAt(position)
                else if (selecting)
                    canvas.controller.updateSelectionDragAt(position)
                else if (points === 1)
                    canvas.updatePanGesture(position.x - previous.x, position.y - previous.y)
                else
                    canvas.controller.transformGesture(scale, previous, position)
            }
            onManipulationFinished: function (canceled) {
                if (movingImport && canvas.controller)
                    canvas.controller.endImportDrag()
                if (selecting && canvas.controller) {
                    if (canceled)
                        canvas.controller.cancelSelectionDrag()
                    else
                        canvas.controller.finishSelectionDrag()
                }
                if (!canceled && activePoints === 1 && !selecting && !movingImport)
                    canvas.endPanGesture()
                else {
                    canvas.endMapDrag()
                    canvas.stopPanInertia()
                }
                activePoints = 0
                selecting = false
                movingImport = false
            }
        }

        WheelHandler {
            id: mapWheelHandler
            enabled: !canvas.inputBlocked && !mapDragHandler.active && mapTouchInput.pointCount === 0
            acceptedDevices: PointerDevice.Mouse
            acceptedModifiers: Qt.NoModifier
            // One ladder step per notch. High-resolution wheels and touchpads deliver
            // many small deltas per gesture, so those accumulate into whole notches
            // instead of firing a step each: a single scroll would otherwise run all
            // the way to the zoom floor.
            property real wheelDelta: 0
            readonly property int notchSize: 120
            readonly property int maxStepsPerEvent: 4
            onWheel: function (event) {
                const wheelAmount = event.angleDelta.y
                if (!canvas.controller || wheelAmount === 0) {
                    event.accepted = false
                    return
                }
                canvas.stopPanInertia()
                // A leftover fraction from the other direction would swallow the
                // first notch of a reversal, so a change of direction starts clean.
                if (wheelDelta !== 0 && (wheelAmount > 0) !== (wheelDelta > 0))
                    wheelDelta = 0
                wheelDelta += wheelAmount
                let steps = Math.trunc(wheelDelta / notchSize)
                if (steps !== 0) {
                    wheelDelta -= steps * notchSize
                    steps = Math.max(-maxStepsPerEvent, Math.min(maxStepsPerEvent, steps))
                    // WheelEvent exposes x/y (in this item's coordinates); it has no
                    // `position` property, and passing undefined would anchor the
                    // zoom at the view's top-left corner instead of the cursor.
                    const anchor = Qt.point(event.x, event.y)
                    for (let i = 0; i < Math.abs(steps); ++i)
                        canvas.controller.zoomAdjacent(steps > 0 ? 1 : -1, anchor)
                }
                event.accepted = true
            }
        }
    }

    Rectangle {
        id: debugPanel
        visible: canvas.showRenderDebug
        z: 5
        // Both debug panels use the same monospace font and size.
        readonly property font panelFont: canvas.debugFont
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 72
        anchors.rightMargin: 18
        // Preserve the monospace tree and allow scrolling on small viewports.
        implicitWidth: 560
        implicitHeight: debugColumn.implicitHeight + 16
        width: Math.max(0, Math.min(implicitWidth, parent.width - 36))
        height: Math.max(0, Math.min(implicitHeight, parent.height - 90))
        radius: Theme.radiusSmall
        color: Theme.overlay
        border.color: Theme.overlayBorder
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: function (eventPoint, button) {
                debugCopyMenu.snapshot = canvas.renderDebugSnapshot()
                debugCopyMenu.popup(debugPanel, eventPoint.position.x, eventPoint.position.y)
            }
        }
        Flickable {
            anchors.fill: parent
            anchors.margins: 8
            contentWidth: Math.max(width, timingTree.implicitWidth)
            contentHeight: debugColumn.implicitHeight
            clip: true
            Column {
                id: debugColumn
                width: Math.max(debugPanel.width - 16, timingTree.implicitWidth)
                spacing: 1
                RowLayout {
                    width: debugPanel.width - 16
                    spacing: 2
                    Text {
                        id: timingHeading
                        Layout.fillWidth: true
                        text: "RENDERER DEBUG  |  %1 FPS".arg(terrainLayer.framesPerSecond.toFixed(1))
                        color: Theme.overlayText
                        font.family: debugPanel.panelFont.family
                        font.pointSize: debugPanel.panelFont.pointSize
                        wrapMode: Text.Wrap
                    }
                    Button {
                        text: "Reset"
                        flat: true
                        font.pixelSize: Theme.fontCaption
                        padding: 2
                        onClicked: terrainLayer.resetTimings()
                    }
                    Button {
                        text: "Copy"
                        flat: true
                        font.pixelSize: Theme.fontCaption
                        padding: 2
                        onClicked: terrainLayer.copyDebugText(canvas.renderDebugSnapshot())
                    }
                }
                Text {
                    id: rendererInfo
                    width: parent.width
                    color: Theme.overlayText
                    font: debugPanel.panelFont
                    text: terrainLayer.graphicsInfo
                    wrapMode: Text.Wrap
                }
                Text {
                    id: timingZoom
                    color: Theme.overlayText
                    font: debugPanel.panelFont
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: "Zoom %1 px/chunk | %2 px/block | LOD %3 blocks/texel".arg(canvas.controller
                                                                                     ? mapOverlay.viewScale.toFixed(2) :
                                                                                       "-").arg(canvas.controller ? (
                                                                                                                        Math.abs(mapOverlay.viewScale)
                                                                                                                        / 16).toFixed(
                                                                                                                        2) : "-").arg(
                              terrainLayer.blocksPerTexel)
                }
                Text {
                    id: atlasState
                    color: Theme.overlayText
                    font: debugPanel.panelFont
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: "Atlas: %1×%2 | terrain %3 | loading %4 | empty %5".arg(terrainLayer.atlasSlotsPerSide).arg(
                              terrainLayer.atlasSlotsPerSide).arg(terrainLayer.terrainSlotCount).arg(
                              terrainLayer.loadingSlotCount).arg(terrainLayer.emptySlotCount)
                }
                Text {
                    id: uploadState
                    width: parent.width
                    color: Theme.overlayText
                    font: debugPanel.panelFont
                    text: "Uploads: %1 terrain tiles".arg(terrainLayer.uploadedTiles)
                }
                Text {
                    id: viewportState
                    width: parent.width
                    color: Theme.overlayText
                    font: debugPanel.panelFont
                    text: "Viewport: %1 × %2 px".arg(Math.round(canvas.width)).arg(Math.round(canvas.height))
                }
                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.overlayBorder
                }
                Text {
                    id: timingTree
                    text: terrainLayer.timingText
                    textFormat: Text.PlainText
                    color: Theme.overlayText
                    font: debugPanel.panelFont
                }
            }
        }
    }
    // Data layer debug panel with cache occupancy/capacity per dimension, thread
    // pool, chunk edits, region load/render timings) plus process memory. The
    // render stats panel above and this loader panel are separate.
    Rectangle {
        id: loaderDebugPanel
        visible: canvas.showRenderDebug
        z: 5
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: canvas.width < debugPanel.width + implicitWidth + 36 ? debugPanel.y + debugPanel.height + 10 :
                                                                                  72
        anchors.leftMargin: 18
        implicitWidth: loaderDebugColumn.implicitWidth + 16
        implicitHeight: loaderDebugColumn.implicitHeight + 16
        width: implicitWidth
        height: implicitHeight
        radius: Theme.radiusSmall
        color: Theme.overlay
        border.color: Theme.overlayBorder
        readonly property font panelFont: canvas.debugFont
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: function (eventPoint, button) {
                const lines = canvas.terrainData ? canvas.terrainData.loaderDebug : []
                debugCopyMenu.snapshot = lines.join("\n") + "\n" + memoryUsage.text
                debugCopyMenu.popup(loaderDebugPanel, eventPoint.position.x, eventPoint.position.y)
            }
        }
        Column {
            id: loaderDebugColumn
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 8
            spacing: 0
            Repeater {
                model: canvas.terrainData ? canvas.terrainData.loaderDebug : []
                delegate: Text {
                    required property string modelData
                    text: modelData
                    color: Theme.overlayText
                    font: loaderDebugPanel.panelFont
                }
            }
            Text {
                id: memoryUsage
                text: qsTr("Memory usage: %1 MiB").arg(canvas.terrainData ? canvas.terrainData.memoryUsageMiB.toFixed(1) :
                                                                            "-")
                color: Theme.overlayText
                font: loaderDebugPanel.panelFont
            }
        }
    }
}

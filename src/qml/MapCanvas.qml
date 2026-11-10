import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import BedrockMap 1.0
import "controls"

Item {
    id: canvas
    // Scene Graph children must stay inside the map viewport. Without this,
    // terrain geometry can paint over the sidebar because QQuickItem does not
    // clip descendants by default.
    clip: true
    property var controller
    property var chunkEditor
    readonly property var regionImport: controller ? controller.regionImport : null
    readonly property bool importActive: !!regionImport && regionImport.active
    readonly property bool biomeDialogOpen: biomeDialog && biomeDialog.opened
    readonly property bool inputBlocked: !controller || controller.operationRunning || (!!regionImport
                                                                                        && regionImport.loading)
                                         || biomeDialogOpen
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
    property bool hoverBlockActive: false
    property var hoverBlockInfo: ({})
    property point lastHoverPosition: Qt.point(0, 0)
    property bool hasLastHoverPosition: false
    property real panInertia: App.runtimeSettings ? App.runtimeSettings.panInertia : 0
    property point panVelocity: Qt.point(0, 0)
    property real lastPanSampleMs: 0
    property real lastPanMotionMs: 0
    property real lastInertiaTickMs: 0
    signal chunkEditorRequested(int chunkX, int chunkZ, int dimension)
    signal voxelViewRequested(int chunkX, int chunkZ, int dimension)
    /// Both debug panels use the platform font.
    readonly property font debugFont: Qt.font({
                                                  pointSize: 8
                                              })

    function releaseInputFocus() {
        if (Window.activeFocusItem)
            Window.activeFocusItem.focus = false
        Qt.inputMethod.hide()
    }

    FileUrlDialog {
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
                owner.beginImportAsync(selectedUrl, targetX, targetZ)
            owner = null
        }
        onRejected: owner = null
    }

    FileUrlDialog {
        id: exportDialog
        title: qsTr("Export selected chunks")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "bchks"
        nameFilters: [qsTr("BedrockMap regions (*.bchks)")]
        onAccepted: if (canvas.controller && canvas.controller.exportSelectionAsync(selectedUrl))
                        canvas.operationText = qsTr("Exporting selected chunks…")
    }

    FileUrlDialog {
        id: screenshotDialog
        title: qsTr("Save screenshot")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [qsTr("PNG images (*.png)")]
        onAccepted: canvas.saveScreenshot(selectedUrl)
    }

    MapBiomeDialog {
        id: biomeDialog
        parent: Overlay.overlay
        onBiomeAccepted: function (value) {
            if (canvas.controller && canvas.controller.setSelectionBiomeAsync(value))
                canvas.operationText = qsTr("Applying biome…")
        }
    }

    MapContextMenu {
        id: mapContextMenu
        operationEnabled: canvas.controller && !canvas.controller.operationRunning
        deleteOutsideEnabled: canvas.controller && canvas.controller.deleteOutsideAvailable
        inputBlocked: canvas.inputBlocked
        importActive: canvas.importActive

        onUnselectRequested: canvas.clearMapSelection()
        onDeleteSelectionRequested: canvas.deleteSelectedChunks()
        onDeleteOutsideSelectionRequested: canvas.deleteChunksOutsideSelection()
        onCreateVoidRequested: canvas.clearSelectedChunks()
        onSetBiomeRequested: canvas.openBiomePicker()
        onCopySelectionRequested: canvas.copySelectedChunks()
        onExportSelectionRequested: canvas.openChunkRegionExport()
        onPasteRequested: {
            const position = canvas.requestedChunkPosition()
            if (position && canvas.controller)
                canvas.controller.beginPasteAsync(position.chunkX, position.chunkZ)
        }
        onImportRequested: {
            const position = canvas.requestedChunkPosition()
            if (!position)
                return
            importDialog.targetX = position.chunkX
            importDialog.targetZ = position.chunkZ
            importDialog.dimension = position.dimension
            importDialog.owner = canvas.controller
            importDialog.open()
        }
        onCopyTextRequested: function (text) {
            if (canvas.controller)
                canvas.controller.copyText(text)
        }
        onScreenshotRequested: canvas.openScreenshotDialog()
        onVoxelViewRequested: canvas.requestVoxelView()
        onChunkEditorRequested: canvas.requestChunkEditor()
    }

    Connections {
        target: canvas.controller
        function onOperationFinished() {
            canvas.operationText = ""
            App.notifications.info(qsTr("Map operation completed"))
        }
        function onOperationFailed(message) {
            canvas.operationText = ""
            App.notifications.error(qsTr("Map operation failed"))
        }
        function onDimensionChanged() {
            canvas.refreshHoverBlock(true)
        }
        function onCameraMoved() {
            canvas.refreshHoverBlock(false)
        }
        function onMapDataChanged(region, dimension) {
            if (canvas.hoverBlockActive && canvas.hoverBlockInfo.dimension === dimension)
                canvas.refreshHoverBlock(true)
        }
        function onWorldStateChanged() {
            canvas.refreshHoverBlock(true)
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

    function confirmImport() {
        if (controller && controller.confirmImport())
            operationText = qsTr("Importing chunks…")
    }

    // Zoom steps come from MapViewState::zoomToAdjacentLevel():
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
    function updateHoverBlock(position, force) {
        if (!controller || inputBlocked) {
            clearHoverBlock()
            return
        }

        lastHoverPosition = position
        hasLastHoverPosition = true
        const info = controller.blockInfoAt(position)
        const blockX = Number(info.blockX)
        const blockZ = Number(info.blockZ)
        const dimension = Number(info.dimension)
        if (!Number.isFinite(blockX) || !Number.isFinite(blockZ) || !Number.isFinite(dimension))
            return
        if (!force && hoverBlockActive && hoverBlockInfo.blockX === blockX && hoverBlockInfo.blockZ === blockZ
                && hoverBlockInfo.dimension === dimension)
            return
        hoverBlockInfo = info
        hoverBlockActive = true
    }
    function refreshHoverBlock(force) {
        if (hasLastHoverPosition)
            updateHoverBlock(lastHoverPosition, force)
    }
    function clearHoverBlock() {
        hoverBlockActive = false
        hasLastHoverPosition = false
        hoverBlockInfo = ({})
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
        const biomeId = hoverBlockActive ? hoverBlockInfo.biomeId : mapContextMenu.blockInfo.biomeId
        biomeDialog.openFor(controller.biomeOptions(), biomeId)
    }
    function copySelectedChunks() {
        if (!controller || !controller.hasSelection || controller.operationRunning)
            return
        if (controller.copySelectionAsync())
            operationText = qsTr("Copying selected chunks…")
    }
    function clearMapSelection() {
        if (controller)
            controller.clearSelection()
    }
    function deleteSelectedChunks() {
        if (!controller || !controller.hasSelection || controller.operationRunning)
            return
        if (controller.deleteSelectionAsync())
            operationText = qsTr("Deleting selected chunks…")
    }
    function deleteChunksOutsideSelection() {
        if (!controller || !controller.hasSelection || controller.operationRunning || !controller.deleteOutsideAvailable)
            return
        if (controller.deleteOutsideSelectionAsync())
            operationText = qsTr("Deleting chunks outside the selection…")
    }
    function clearSelectedChunks() {
        if (!controller || !controller.hasSelection || controller.operationRunning)
            return
        if (controller.createVoidSelectionAsync())
            operationText = qsTr("Clearing selected chunks…")
    }
    function pasteAtRequestedPosition() {
        const position = requestedChunkPosition()
        if (position && controller)
            controller.beginPasteAsync(position.chunkX, position.chunkZ)
    }
    function openChunkImportDialog() {
        const position = requestedChunkPosition()
        if (!position)
            return
        importDialog.targetX = position.chunkX
        importDialog.targetZ = position.chunkZ
        importDialog.dimension = position.dimension
        importDialog.owner = controller
        importDialog.open()
    }
    function openChunkRegionExport() {
        exportDialog.selectedFile = exportDialog.currentFolder + "/chunkregion.bchks"
        exportDialog.open()
    }
    function openScreenshotDialog() {
        screenshotDialog.selectedFile = screenshotDialog.currentFolder + "/map-screenshot.png"
        screenshotDialog.open()
    }
    function saveScreenshot(targetFile) {
        canvas.grabToImage(function (result) {
            if (!result || !App.saveImage(targetFile, result.image)) {
                App.notifications.error(qsTr("Save failed"))
                return
            }
            App.notifications.info(qsTr("Save successful"))
        })
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
        const source = hoverBlockActive ? hoverBlockInfo : mapContextMenu.blockInfo
        const blockX = Number(source.blockX)
        const blockZ = Number(source.blockZ)
        if (!Number.isFinite(blockX) || !Number.isFinite(blockZ))
            return null
        return {
            chunkX: Math.floor(blockX / 16),
            chunkZ: Math.floor(blockZ / 16),
            dimension: Number.isFinite(Number(source.dimension)) ? Number(source.dimension) : mapContextMenu.dimension
        }
    }
    onWidthChanged: if (controller)
                        controller.setViewportSize(Qt.size(width, height))
    onHeightChanged: if (controller)
                         controller.setViewportSize(Qt.size(width, height))
    Component.onCompleted: if (controller)
                               controller.setViewportSize(Qt.size(width, height))
    onControllerChanged: {
        stopPanInertia()
        clearHoverBlock()
        if (mapTouchInput)
            mapTouchInput.cancelGesture()
        if (controller)
            controller.setViewportSize(Qt.size(width, height))
    }
    onInputRightInsetChanged: {
        stopPanInertia()
        refreshHoverBlock(true)
        if (mapTouchInput)
            mapTouchInput.cancelGesture()
    }
    onVisibleChanged: if (!visible) {
                          stopPanInertia()
                          clearHoverBlock()
                          mapTouchInput.cancelGesture()
                          if (controller)
                              controller.endImportDrag()
                      }
    onInputBlockedChanged: {
        if (!inputBlocked)
            return
        clearHoverBlock()
        stopPanInertia()
        if (mapTouchInput)
            mapTouchInput.cancelGesture()
        if (controller && controller.selectionDragging)
            controller.cancelSelectionDrag()
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
        chunkEditor: canvas.chunkEditor
        entitiesVisible: canvas.showEntities
        slimeChunksVisible: canvas.showSlimeChunks
        villagesVisible: canvas.showVillages
        hsaVisible: canvas.showHsa
        actorRenderStyle: App.runtimeSettings ? App.runtimeSettings.actorRenderStyle : 0
        gridVisible: canvas.showGrid
        gridColor: App.runtimeSettings ? App.runtimeSettings.gridLineColor : "#bbbbbb"
        selectionColor: App.runtimeSettings ? App.runtimeSettings.mapSelectionColor : "#50dafffb"
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
        active: canvas.inputBlocked && !canvas.biomeDialogOpen && !!canvas.controller
        text: canvas.regionImport && canvas.regionImport.loading ? qsTr("Loading region…") : canvas.operationText
    }

    MapImportPlacementOverlay {
        id: importPlacementOverlay
        anchors.fill: parent
        z: 10
        regionImport: canvas.regionImport
        inputBlocked: canvas.inputBlocked
        inputRightInset: canvas.inputRightInset
        onCancelRequested: if (canvas.controller)
                               canvas.controller.cancelImport()
        onConfirmRequested: canvas.confirmImport()
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
        anchors.bottomMargin: importPlacementOverlay.reservedHeight
        anchors.left: parent.left
        width: Math.max(0, canvas.width - canvas.inputRightInset)
        z: 4

        HoverHandler {
            id: mapHover
            enabled: !canvas.inputBlocked && !canvas.importActive
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onHoveredChanged: {
                if (hovered)
                    canvas.updateHoverBlock(point.position, true)
                else
                    canvas.clearHoverBlock()
            }
            onPointChanged: if (hovered)
                                canvas.updateHoverBlock(point.position, false)
        }

        TapHandler {
            enabled: !canvas.inputBlocked && mapTouchInput.pointCount === 0
            acceptedDevices: PointerDevice.Mouse
            acceptedButtons: Qt.RightButton
            onTapped: function (eventPoint) {
                canvas.releaseInputFocus()
                canvas.openMapContextMenu(eventPoint.position)
            }
        }

        TapHandler {
            enabled: !canvas.inputBlocked && mapTouchInput.pointCount === 0
            acceptedDevices: PointerDevice.Mouse
            acceptedButtons: Qt.LeftButton
            onPressedChanged: if (pressed) {
                                  canvas.releaseInputFocus()
                                  canvas.stopPanInertia()
                              }
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
                    canvas.releaseInputFocus()
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
                canvas.releaseInputFocus()
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

    AppCard {
        id: mapStatusOverlay
        visible: !!canvas.controller
        z: 6
        readonly property real edgeMargin: Theme.gapLarge
        readonly property real innerPadding: Theme.gapMedium
        readonly property real availableWidth: Math.max(0, parent.width - canvas.inputRightInset - edgeMargin * 2)
        readonly property string positionText: canvas.hoverBlockActive ? "Dim: %1 Pos:%2,%3/%4,%5".arg(
                                                                          canvas.controller.dimension).arg(
                                                                          canvas.hoverBlockInfo.blockX).arg(
                                                                          canvas.hoverBlockInfo.blockZ).arg(
                                                                          canvas.hoverBlockInfo.chunkX).arg(
                                                                          canvas.hoverBlockInfo.chunkZ) :
                                                                      "Dim: %1 Pos:--,--/--,--".arg(
                                                                          canvas.controller.dimension)
        readonly property string countsText: "Sel:%1 Modify:%2 Del:%3".arg(
                                                 canvas.controller.selectionChunkCount).arg(
                                                 canvas.controller.modifiedChunkCount).arg(
                                                 canvas.controller.deletedChunkCount)
        readonly property real contentWidth: Math.max(positionMetrics.advanceWidth, countsMetrics.advanceWidth)
        width: Math.min(Math.max(Theme.ui(190), contentWidth + innerPadding * 2), availableWidth)
        height: statusColumn.implicitHeight + innerPadding * 2
        x: Math.max(edgeMargin, parent.width - canvas.inputRightInset - edgeMargin - width)
        y: Math.max(edgeMargin, parent.height - importPlacementOverlay.reservedHeight - edgeMargin - height)
        color: Theme.surface
        radius: Theme.radiusCard
        border.width: Theme.borderWidth
        border.color: Theme.border
        clip: true

        TextMetrics {
            id: positionMetrics
            font.family: "JetBrains Mono"
            font.pixelSize: Theme.fontBody
            text: mapStatusOverlay.positionText
        }
        TextMetrics {
            id: countsMetrics
            font.family: "JetBrains Mono"
            font.pixelSize: Theme.fontBody
            text: mapStatusOverlay.countsText
        }

        Column {
            id: statusColumn
            x: mapStatusOverlay.innerPadding
            y: mapStatusOverlay.innerPadding
            width: parent.width - mapStatusOverlay.innerPadding * 2
            spacing: Theme.gapSmall

            Text {
                width: parent.width
                text: mapStatusOverlay.positionText
                color: Theme.text
                font.family: "JetBrains Mono"
                font.pixelSize: Theme.fontBody
                elide: Text.ElideRight
            }
            Row {
                width: parent.width
                Text {
                    text: "Sel:%1 ".arg(canvas.controller.selectionChunkCount)
                    color: Theme.text
                    font.family: "JetBrains Mono"
                    font.pixelSize: Theme.fontBody
                }
                Text {
                    text: "Modify:%1 ".arg(canvas.controller.modifiedChunkCount)
                    color: canvas.controller.modifiedChunkCount > 0 ? Theme.danger : Theme.text
                    font.family: "JetBrains Mono"
                    font.pixelSize: Theme.fontBody
                }
                Text {
                    text: "Del:%1".arg(canvas.controller.deletedChunkCount)
                    color: canvas.controller.deletedChunkCount > 0 ? Theme.danger : Theme.text
                    font.family: "JetBrains Mono"
                    font.pixelSize: Theme.fontBody
                }
            }
        }
    }

    MapRenderDebugPanel {
        anchors.fill: parent
        z: 5
        visible: canvas.showRenderDebug
        terrainLayer: terrainLayer
        mapOverlay: mapOverlay
        terrainData: canvas.terrainData
        debugFont: canvas.debugFont
    }
}

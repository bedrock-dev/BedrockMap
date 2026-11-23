import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "controls"

Item {
    id: workspace

    readonly property PageGroup childPages: PageGroup {
        rootRoute: "root"
        definitions: [
            {
                route: "root",
                title: ""
            },
            {
                route: "chunk-editor",
                title: qsTr("Chunk editor")
            },
            {
                route: "voxel",
                title: qsTr("3D view")
            }
        ]
        titleOverride: {
            if (workspace.chunkEditorOpen)
                return chunkEditorPanel.headerTitle
            if (workspace.voxelViewOpen)
                return voxelOverviewPanel.headerTitle
            return ""
        }
    }

    property var session
    property var mapController: session ? session.mapController : null
    property var terrainData: session ? session.terrainData : null
    property var globalSections: session && session.globalData ? session.globalData.sections : []
    // Overlay switches live in the session's map state, so each world keeps its own.
    readonly property var overlays: mapController ? mapController.overlays : ({})
    property int section: 0
    property bool restoringSection: false
    property bool compact: Theme.compact
    readonly property int selectionChunkCount: mapController ? mapController.selectionChunkCount : 0
    property bool layerPanelOpen: false
    property bool mapFilterPanelOpen: false
    property bool debugVisible: false
    property var pendingChunkOpen: null
    readonly property bool chunkEditorOpen: childPages.currentRoute === "chunk-editor"
    readonly property bool voxelViewOpen: childPages.currentRoute === "voxel"
    readonly property bool importPending: !!mapController && !!mapController.regionImport && (
                                              mapController.regionImport.active || mapController.regionImport.loading)
    // Back first cancels a pending region import, then closes the child page.
    readonly property bool canGoBack: importPending || childPages.isChildOpen
    readonly property bool textInputFocused: Window.activeFocusItem instanceof TextInput
                                             || Window.activeFocusItem instanceof TextEdit
    readonly property bool mapShortcutsEnabled: section === 0 && !childPages.isChildOpen && !!mapController &&
                                                !mapController.operationRunning && !importPending && !textInputFocused
    property real sidePanelWidth: 0
    signal closeRequested
    signal saveRequested

    readonly property var workspaceEntries: {
        const entries = [
                  {
                      id: "map",
                      kind: "section",
                      title: qsTr("Map"),
                      label: qsTr("Map"),
                      iconName: "map",
                      iconText: "",
                      navigationIndex: 0,
                      dirty: false,
                      selected: !session || session.titleBarNavigationIndex === 0,
                      enabled: true,
                      beginGroup: false
                  }
              ]
        const chunkEditor = session ? session.chunkEditor : null
        entries.push({
                         id: "chunk-editor",
                         kind: "chunkEditor",
                         title: qsTr("Chunk editor"),
                         label: qsTr("Chunk editor"),
                         iconName: "nbt_editor",
                         iconText: "",
                         dirty: !!chunkEditor && chunkEditor.dirty,
                         selected: workspace.chunkEditorOpen,
                         enabled: !!chunkEditor && (chunkEditor.hasChunk || chunkEditor.loading),
                         beginGroup: false
                     })
        const voxelView = session ? session.voxelView : null
        entries.push({
                         id: "voxel-view",
                         kind: "voxelView",
                         title: qsTr("3D view"),
                         label: qsTr("3D view"),
                         iconName: "structure",
                         iconText: "",
                         dirty: false,
                         selected: workspace.voxelViewOpen,
                         enabled: !!voxelView && voxelView.hasVoxelModel,
                         beginGroup: false
                     })
        const sections = session && session.titleBarNavigationVisible ? session.titleBarNavigationSections : []
        for (let i = 0; i < sections.length; ++i) {
            const section = sections[i]
            entries.push({
                             id: "section-" + i,
                             kind: "section",
                             title: section.title,
                             label: section.title + (section.showCount ? " " + section.count : ""),
                             iconName: section.iconName ? section.iconName : "nbt_editor",
                             iconText: section.shortLabel || "",
                             navigationIndex: i + 1,
                             dirty: !!section.dirty,
                             selected: !!session && session.titleBarNavigationIndex === i + 1,
                             enabled: true,
                             beginGroup: i === 0
                         })
        }
        return entries
    }
    readonly property var currentWorkspaceEntry: {
        const selected = workspaceEntries.find(entry => entry.selected)
        return selected || workspaceEntries[0] || null
    }

    function activateWorkspaceEntry(entry) {
        if (!entry || entry.enabled === false)
            return
        if (entry.kind === "chunkEditor") {
            toggleChunkEditor()
            return
        }
        if (entry.kind === "voxelView") {
            toggleVoxelView()
            return
        }
        if (session && session.titleBarNavigationVisible)
            session.titleBarNavigationIndex = entry.navigationIndex
    }

    readonly property var workspaceShortcutEntries: ShortcutRegistry.map.concat(ShortcutRegistry.chunk)

    // Shortcut definitions are shared, while their actions remain owned by the
    // workspace that has access to the current map controller and canvas.
    function runWorkspaceShortcut(command) {
        switch (command) {
        case "toggleGrid":
            toggleMapOverlay("Grid")
            return
        case "toggleCoordinates":
            toggleMapOverlay("Coords")
            return
        case "dimensionOverworld":
            mapController.setDimension(0)
            return
        case "dimensionNether":
            mapController.setDimension(1)
            return
        case "dimensionEnd":
            mapController.setDimension(2)
            return
        case "dimensionCustom":
            openCustomDimensionDialog()
            return
        case "layerTerrain":
            mapController.setLayer(0)
            return
        case "layerBiome":
            mapController.setLayer(1)
            return
        case "toggleSlime":
            toggleMapOverlay("SlimeChunk")
            return
        case "toggleEntities":
            toggleMapOverlay("Actors")
            return
        case "toggleVillages":
            toggleMapOverlay("Village")
            return
        case "toggleHsa":
            toggleMapOverlay("HSA")
            return
        case "toggleTransparentVoid":
            mapController.setTransparentVoid(!mapController.transparentVoid)
            return
        case "toggleFilters":
            toggleMapFilterPanel()
            return
        case "toggleDebug":
            debugVisible = !debugVisible
            return
        case "selectionReplace":
            mapController.setSelectionMode(0)
            return
        case "selectionAdd":
            mapController.setSelectionMode(1)
            return
        case "selectionSubtract":
            mapController.setSelectionMode(2)
            return
        case "clearSelection":
            canvas.clearMapSelection()
            return
        case "copySelection":
            canvas.copySelectedChunks()
            return
        case "pasteSelection":
            canvas.pasteAtRequestedPosition()
            return
        case "exportSelection":
            canvas.openChunkRegionExport()
            return
        case "importSelection":
            canvas.openChunkImportDialog()
            return
        case "deleteSelection":
            canvas.deleteSelectedChunks()
            return
        case "clearChunks":
            canvas.clearSelectedChunks()
            return
        case "editBiome":
            canvas.openBiomePicker()
            return
        case "screenshot":
            canvas.openScreenshotDialog()
            return
        case "openVoxel":
            toggleVoxelView()
            return
        case "focusCoordinates":
            focusGotoFields()
            return
        }
    }

    // Shortcut needs an Item parent to find its window, hence the Item wrapper.
    Repeater {
        model: workspace.workspaceShortcutEntries
        delegate: Item {
            id: shortcutHost
            required property var modelData
            Shortcut {
                sequence: shortcutHost.modelData.sequence
                enabled: workspace.mapShortcutsEnabled && (!shortcutHost.modelData.requiresSelection
                                                           || workspace.mapController.hasSelection)
                onActivated: workspace.runWorkspaceShortcut(shortcutHost.modelData.command)
            }
        }
    }

    Connections {
        target: workspace.childPages
        function onNavigated(previousRoute, nextRoute) {
            if (workspace.session)
                workspace.session.workspaceRoute = nextRoute
        }
    }

    Connections {
        target: workspace.session
        function onWorkspaceSectionChanged() {
            workspace.restoreSection()
        }
    }

    function openCustomDimensionDialog() {
        if (!mapController)
            return
        const options = mapController.customDimensionOptions()
        if (options.length === 0) {
            App.notifications.info(qsTr("This world has no custom dimensions."))
            return
        }
        customDimensionDialog.openFor(options, mapController.dimension)
    }
    function toggleMapOverlay(name) {
        const overlay = overlays[name]
        if (mapController && overlay)
            mapController.toggleOverlay(Number(overlay.type))
    }
    function toggleLayerPanel() {
        layerPanelOpen = !layerPanelOpen
        if (layerPanelOpen)
            mapFilterPanelOpen = false
    }
    function toggleMapFilterPanel() {
        mapFilterPanelOpen = !mapFilterPanelOpen
        if (mapFilterPanelOpen)
            layerPanelOpen = false
    }
    function focusGotoFields() {
        xField.forceActiveFocus()
        xField.selectAll()
    }
    function jumpToCoordinates() {
        if (!mapController || !xField.acceptableInput || !zField.acceptableInput)
            return false
        const x = Number(xField.text)
        const z = Number(zField.text)
        if (!Number.isFinite(x) || !Number.isFinite(z))
            return false
        mapController.gotoBlock(Math.round(x), Math.round(z))
        return true
    }
    function submitCoordinates() {
        if (!jumpToCoordinates())
            return false
        xField.focus = false
        zField.focus = false
        Qt.inputMethod.hide()
        return true
    }
    function acceptXCoordinateInput() {
        if (!submitCoordinates()) {
            zField.forceActiveFocus()
            zField.selectAll()
        }
    }
    function acceptZCoordinateInput() {
        submitCoordinates()
    }
    function openChunkEditorNow(chunkX, chunkZ, dimension) {
        if (!session || !session.chunkEditor)
            return
        session.chunkEditor.openChunk(chunkX, chunkZ, dimension)
        canvas.selectionToolActive = false
        childPages.open("chunk-editor")
    }
    function openChunkEditor(chunkX, chunkZ, dimension) {
        if (!session || !session.chunkEditor)
            return
        if (session.chunkEditor.dirty) {
            pendingChunkOpen = {
                "chunkX": chunkX,
                "chunkZ": chunkZ,
                "dimension": dimension
            }
            chunkSwitchDialog.open()
            return
        }
        openChunkEditorNow(chunkX, chunkZ, dimension)
    }
    function savePendingChunkOpen() {
        if (!pendingChunkOpen || !session || !session.chunkEditor)
            return
        if (!session.chunkEditor.saveToCache()) {
            AppErrorReporter.report(qsTr("Save failed"), session.chunkEditor)
            return
        }
        const target = pendingChunkOpen
        pendingChunkOpen = null
        chunkSwitchDialog.close()
        openChunkEditorNow(target.chunkX, target.chunkZ, target.dimension)
    }
    function discardPendingChunkOpen() {
        if (!pendingChunkOpen)
            return
        const target = pendingChunkOpen
        pendingChunkOpen = null
        chunkSwitchDialog.close()
        openChunkEditorNow(target.chunkX, target.chunkZ, target.dimension)
    }
    function cancelPendingChunkOpen() {
        pendingChunkOpen = null
        chunkSwitchDialog.close()
    }
    function toggleChunkEditor() {
        if (chunkEditorOpen) {
            childPages.pop()
            return
        }
        if (!session || !session.chunkEditor || (!session.chunkEditor.hasChunk && !session.chunkEditor.loading))
            return
        canvas.selectionToolActive = false
        childPages.open("chunk-editor")
    }
    function openVoxelView() {
        if (!mapController || !session || !session.voxelView)
            return
        const area = mapController.voxelViewArea()
        if (!area || area.minX === undefined)
            return
        session.voxelView.openArea(area.minX, area.minZ, area.maxX, area.maxZ, area.dimension)
        canvas.selectionToolActive = false
        childPages.open("voxel")
    }
    function toggleVoxelView() {
        if (voxelViewOpen) {
            childPages.pop()
            return
        }
        if (!session || !session.voxelView || !session.voxelView.hasVoxelModel)
            return
        canvas.selectionToolActive = false
        childPages.open("voxel")
    }
    function clampSidePanelWidth(value) {
        const available = Math.max(1, sectionHost.width)
        return Math.max(280, Math.min(value, Math.max(280, available - 180)))
    }
    function currentSidePanelWidth() {
        return sidePanelWidth > 0 ? clampSidePanelWidth(sidePanelWidth) : sectionHost.width * 0.5
    }
    function maximumInspectorPanelHeight() {
        return Math.max(0, layerToolbar.y - navigationToolbar.y - navigationToolbar.height - Theme.gapSmall * 2)
    }
    function goBack() {
        if (importPending) {
            mapController.cancelImport()
            return true
        }
        return childPages.pop()
    }

    function restoreSection() {
        restoringSection = true
        section = session ? session.workspaceSection : 0
        restoringSection = false
    }
    function restoreSessionState() {
        restoreSection()
        const route = session ? session.workspaceRoute : "root"
        childPages.reset(childPages.routeDefinition(route) ? route : "root")
        // onSectionChanged closes the panels only when leaving the map section.
        layerPanelOpen = false
        mapFilterPanelOpen = false
    }

    onSessionChanged: restoreSessionState()
    Component.onCompleted: restoreSessionState()
    onGlobalSectionsChanged: if (session && section >= globalSections.length + 1)
                                 section = 0
    onSectionChanged: {
        if (section !== 0) {
            if (canvas)
                canvas.selectionToolActive = false
            layerPanelOpen = false
            mapFilterPanelOpen = false
        }
        if (!restoringSection && session)
            session.workspaceSection = section
    }
    Item {
        id: sectionHost
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: workspace.compact ? parent.right : desktopSidebar.left

        Item {
            id: mapSection
            anchors.fill: parent
            visible: workspace.section === 0

            MapCanvas {
                id: canvas
                anchors.fill: parent
                inputRightInset: editorPanelHost.visible ? editorPanelHost.width : 0
                controller: workspace.mapController
                terrainData: workspace.terrainData
                globalData: workspace.session ? workspace.session.globalData : null
                chunkEditor: workspace.session ? workspace.session.chunkEditor : null
                showGrid: !!(workspace.overlays.Grid && workspace.overlays.Grid.enabled)
                showCoordinates: !!(workspace.overlays.Coords && workspace.overlays.Coords.enabled)
                showEntities: !!(workspace.overlays.Actors && workspace.overlays.Actors.enabled)
                showSlimeChunks: !!(workspace.overlays.SlimeChunk && workspace.overlays.SlimeChunk.enabled)
                showVillages: !!(workspace.overlays.Village && workspace.overlays.Village.enabled)
                showHsa: !!(workspace.overlays.HSA && workspace.overlays.HSA.enabled)
                showDimensionBorder: !!(workspace.overlays.DimensionBorder
                                        && workspace.overlays.DimensionBorder.enabled)
                showTerrainOutline: !!(workspace.overlays.TerrainOutline && workspace.overlays.TerrainOutline.enabled)
                showRenderDebug: workspace.debugVisible && App.runtimeSettings.showMapDebugButton
                onChunkEditorRequested: (chunkX, chunkZ, dimension) => workspace.openChunkEditor(chunkX, chunkZ,
                                                                                                 dimension)
                onVoxelViewRequested: function (chunkX, chunkZ, dimension) {
                    workspace.openVoxelView()
                }
            }

            Item {
                id: editorPanelHost
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                width: workspace.compact ? parent.width : workspace.currentSidePanelWidth()
                visible: workspace.chunkEditorOpen || workspace.voxelViewOpen
                z: 8

                // Consume unused panel space before events can reach the map.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.AllButtons
                    preventStealing: true
                    hoverEnabled: true

                    onPressed: function (mouse) {
                        mouse.accepted = true
                    }
                    onWheel: function (wheel) {
                        wheel.accepted = true
                    }
                }

                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    blocking: true
                    onWheel: function (wheel) {
                        wheel.accepted = true
                    }
                }

                ChunkEditorPanel {
                    id: chunkEditorPanel
                    anchors.fill: parent
                    visible: workspace.chunkEditorOpen
                    headerVisible: !workspace.compact
                    z: 1
                    controller: workspace.session ? workspace.session.chunkEditor : null
                    onCloseRequested: workspace.childPages.pop()
                }

                VoxelOverviewPanel {
                    id: voxelOverviewPanel
                    anchors.fill: parent
                    visible: workspace.voxelViewOpen
                    headerVisible: !workspace.compact
                    z: 1
                    controller: workspace.session ? workspace.session.voxelView : null
                    onCloseRequested: workspace.childPages.pop()
                }
            }

            Item {
                id: sidePanelSplitter
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                x: parent.width - workspace.currentSidePanelWidth() - width / 2
                width: 10
                visible: !workspace.compact && (workspace.chunkEditorOpen || workspace.voxelViewOpen)
                z: 10

                Rectangle {
                    anchors.centerIn: parent
                    width: Theme.ui(3)
                    height: Math.max(1, parent.height - Theme.gapSmall * 2)
                    radius: width / 2
                    color: splitterMouse.pressed || splitterMouse.containsMouse ? Theme.accent : Theme.border
                }

                MouseArea {
                    id: splitterMouse
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    preventStealing: true
                    hoverEnabled: true
                    cursorShape: Qt.SplitHCursor
                    property real startWidth: 0
                    property real startSceneX: 0

                    onPressed: function (mouse) {
                        mouse.accepted = true
                        startWidth = workspace.currentSidePanelWidth()
                        startSceneX = mapToItem(null, mouse.x, mouse.y).x
                    }
                    onPositionChanged: function (mouse) {
                        if (!pressed)
                            return
                        const currentX = mapToItem(null, mouse.x, mouse.y).x
                        workspace.sidePanelWidth = workspace.clampSidePanelWidth(startWidth + startSceneX - currentX)
                    }
                }
            }

            MapLayerPanel {
                id: layerPanel
                visible: workspace.layerPanelOpen
                z: 2
                width: Math.max(0, Math.min(360, parent.width - Theme.gapMedium * 2))
                height: Math.min(naturalHeight, workspace.maximumInspectorPanelHeight())
                anchors.bottom: layerToolbar.top
                anchors.bottomMargin: Theme.gapSmall
                anchors.left: layerToolbar.left
                controller: workspace.mapController
                terrainData: workspace.terrainData
                compact: workspace.compact
                debugVisible: workspace.debugVisible
                showDebugToggle: App.runtimeSettings.showMapDebugButton
                onCustomDimensionRequested: workspace.openCustomDimensionDialog()
                onDebugToggleRequested: workspace.debugVisible = !workspace.debugVisible
            }

            AppCard {
                id: navigationToolbar
                z: 1
                readonly property real horizontalPadding: Theme.gapMedium
                readonly property real verticalPadding: Theme.gapSmall
                width: navigationRow.width + horizontalPadding * 2
                height: navigationRow.height + verticalPadding * 2
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: Theme.gapLarge
                Row {
                    id: navigationRow
                    x: navigationToolbar.horizontalPadding
                    height: Theme.touch ? Theme.controlHeight : 34
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.gapTiny
                    AppIconButton {
                        iconName: "zoom_in"
                        label: qsTr("Zoom in")
                        onClicked: canvas.zoomIn()
                    }
                    AppIconButton {
                        iconName: "zoom_out"
                        label: qsTr("Zoom out")
                        onClicked: canvas.zoomOut()
                    }
                    AppIconButton {
                        iconName: "locate"
                        label: qsTr("Return to origin")
                        onClicked: canvas.centerOnOrigin()
                    }
                    AppTextField {
                        id: xField
                        width: workspace.compact ? 72 : 96
                        placeholderText: "X"
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        validator: IntValidator {}
                        anchors.verticalCenter: parent.verticalCenter
                        onAccepted: workspace.acceptXCoordinateInput()
                    }
                    AppTextField {
                        id: zField
                        width: workspace.compact ? 72 : 96
                        placeholderText: "Z"
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        validator: IntValidator {}
                        anchors.verticalCenter: parent.verticalCenter
                        onAccepted: workspace.acceptZCoordinateInput()
                    }
                    AppIconButton {
                        visible: Qt.platform.os === "android"
                        iconName: "enter"
                        label: qsTr("Go to coordinates")
                        onClicked: {
                            Qt.inputMethod.commit()
                            workspace.acceptZCoordinateInput()
                        }
                    }
                }
            }

            MapFilterPanel {
                id: mapFilterPanel
                visible: workspace.mapFilterPanelOpen
                z: 2
                width: Math.max(0, Math.min(360, parent.width - Theme.gapMedium * 2))
                height: Math.min(naturalHeight, workspace.maximumInspectorPanelHeight())
                anchors.bottom: layerToolbar.top
                anchors.bottomMargin: Theme.gapSmall
                anchors.left: layerToolbar.left
                controller: workspace.mapController
                onCloseRequested: workspace.mapFilterPanelOpen = false
            }

            AppCard {
                id: layerToolbar
                z: 3
                readonly property real toolbarPadding: Theme.gapSmall
                width: layerToolbarRow.width + toolbarPadding * 2
                height: layerToolbarRow.height + toolbarPadding * 2
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: Theme.gapLarge

                Row {
                    id: layerToolbarRow
                    x: layerToolbar.toolbarPadding
                    height: Theme.touch ? Theme.controlHeight : 34
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.gapTiny

                    AppIconButton {
                        id: layerButton
                        iconName: "layers"
                        label: qsTr("Layers")
                        selected: workspace.layerPanelOpen
                        onClicked: workspace.toggleLayerPanel()
                    }

                    AppIconButton {
                        id: mapFilterButton
                        iconName: "filter"
                        label: qsTr("Map filters")
                        selected: workspace.mapFilterPanelOpen
                        onClicked: workspace.toggleMapFilterPanel()
                    }

                    AppIconButton {
                        visible: Theme.touch
                        enabled: !canvas.importActive && !canvas.inputBlocked
                        iconName: "selection"
                        label: canvas.selectionToolActive ? qsTr("Exit selection mode (%1 chunks)").arg(
                                                                workspace.selectionChunkCount) : qsTr("Selection mode")
                        selected: canvas.selectionToolActive
                        onClicked: canvas.selectionToolActive = !canvas.selectionToolActive
                    }
                }
            }
        }

        GlobalNbtPage {
            anchors.fill: parent
            visible: workspace.section >= 1
            section: workspace.section >= 1 && workspace.section - 1 < workspace.globalSections.length
                     ? workspace.globalSections[workspace.section - 1] : null
        }
    }

    WorkspaceSidebar {
        id: desktopSidebar
        visible: !workspace.compact
        z: 1
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        session: workspace.session
        entries: workspace.workspaceEntries
        onEntryRequested: entry => workspace.activateWorkspaceEntry(entry)
        onSaveRequested: workspace.saveRequested()
        onCloseRequested: workspace.closeRequested()
    }

    AppBusyOverlay {
        anchors.fill: parent
        z: 40
        active: !!workspace.session && workspace.session.saving
        text: qsTr("Saving world…")
    }

    AppDialog {
        id: chunkSwitchDialog
        title: qsTr("Unsaved changes")
        contentItem: ColumnLayout {
            spacing: Theme.gapMedium

            Text {
                Layout.fillWidth: true
                text: qsTr("This chunk has unsaved changes. Save them before opening a different chunk?")
                color: Theme.text
                font.pixelSize: Theme.fontMedium
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gapMedium

                Item {
                    Layout.fillWidth: true
                }

                AppButton {
                    text: qsTr("Cancel")
                    onClicked: workspace.cancelPendingChunkOpen()
                }
                AppButton {
                    text: qsTr("Discard")
                    danger: true
                    onClicked: workspace.discardPendingChunkOpen()
                }
                AppButton {
                    text: qsTr("Save")
                    primary: true
                    onClicked: workspace.savePendingChunkOpen()
                }
            }
        }
        onClosed: {
            if (workspace.pendingChunkOpen)
                workspace.pendingChunkOpen = null
        }
    }

    CustomDimensionDialog {
        id: customDimensionDialog
        onDimensionAccepted: dimension => workspace.mapController.setDimension(dimension)
    }
}

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
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
    property int section: 0
    property bool restoringSection: false
    property bool compact: Theme.compact
    property bool layerPanelOpen: false
    property bool mapFilterPanelOpen: false
    property bool debugVisible: false
    readonly property bool chunkEditorOpen: childPages.currentRoute === "chunk-editor"
    readonly property bool voxelViewOpen: childPages.currentRoute === "voxel"
    readonly property bool importPending: !!mapController && !!mapController.regionImport
                                          && (mapController.regionImport.active || mapController.regionImport.loading)
    // Back first cancels a pending region import, then closes the child page.
    readonly property bool canGoBack: importPending || childPages.isChildOpen
    readonly property bool textInputFocused: Window.activeFocusItem instanceof TextInput
                                               || Window.activeFocusItem instanceof TextEdit
    readonly property bool mapShortcutsEnabled: section === 0 && !childPages.isChildOpen && !!mapController
                                                && !mapController.operationRunning && !importPending
                                                && !textInputFocused
    property real sidePanelWidth: 0
    signal closeRequested
    signal saveRequested

    Connections {
        target: workspace.childPages
        function onNavigated(previousRoute, nextRoute) {
            if (workspace.session)
                workspace.session.workspaceRoute = nextRoute
        }
    }

    component MapShortcut: Shortcut {
        property bool needsSelection: false
        enabled: workspace.mapShortcutsEnabled && (!needsSelection || workspace.mapController.hasSelection)
    }

    MapShortcut {
        sequence: "Alt+G"
        onActivated: workspace.toggleGrid()
    }
    MapShortcut {
        sequence: "Alt+C"
        onActivated: workspace.toggleCoordinates()
    }
    MapShortcut {
        sequence: "Alt+1"
        onActivated: workspace.setMapDimension(0)
    }
    MapShortcut {
        sequence: "Alt+2"
        onActivated: workspace.setMapDimension(1)
    }
    MapShortcut {
        sequence: "Alt+3"
        onActivated: workspace.setMapDimension(2)
    }
    MapShortcut {
        sequence: "Alt+4"
        onActivated: workspace.openCustomDimensionDialog()
    }
    MapShortcut {
        sequence: "Alt+T"
        onActivated: workspace.setMapLayer(0)
    }
    MapShortcut {
        sequence: "Alt+B"
        onActivated: workspace.setMapLayer(1)
    }
    MapShortcut {
        sequence: "Alt+S"
        onActivated: workspace.toggleSlimeChunks()
    }
    MapShortcut {
        sequence: "Alt+A"
        onActivated: workspace.toggleEntities()
    }
    MapShortcut {
        sequence: "Alt+V"
        onActivated: workspace.toggleVillages()
    }
    MapShortcut {
        sequence: "Alt+H"
        onActivated: workspace.toggleHsa()
    }
    MapShortcut {
        sequence: "Alt+0"
        onActivated: workspace.toggleTransparentVoid()
    }
    MapShortcut {
        sequence: "Alt+F"
        onActivated: workspace.toggleMapFilterPanel()
    }
    MapShortcut {
        sequence: "Alt+D"
        onActivated: workspace.toggleDebugPanels()
    }
    MapShortcut {
        sequence: "Shift+R"
        onActivated: workspace.setMapSelectionMode(0)
    }
    MapShortcut {
        sequence: "Shift+A"
        onActivated: workspace.setMapSelectionMode(1)
    }
    MapShortcut {
        sequence: "Shift+S"
        onActivated: workspace.setMapSelectionMode(2)
    }
    MapShortcut {
        sequence: "Shift+C"
        onActivated: canvas.clearMapSelection()
    }
    MapShortcut {
        sequence: "Ctrl+C"
        needsSelection: true
        onActivated: canvas.copySelectedChunks()
    }
    MapShortcut {
        sequence: "Ctrl+V"
        onActivated: canvas.pasteAtRequestedPosition()
    }
    MapShortcut {
        sequence: "Ctrl+E"
        needsSelection: true
        onActivated: canvas.openChunkRegionExport()
    }
    MapShortcut {
        sequence: "Ctrl+I"
        onActivated: canvas.openChunkImportDialog()
    }
    MapShortcut {
        sequence: "Ctrl+D"
        needsSelection: true
        onActivated: canvas.deleteSelectedChunks()
    }
    MapShortcut {
        sequence: "Ctrl+K"
        needsSelection: true
        onActivated: canvas.clearSelectedChunks()
    }
    MapShortcut {
        sequence: "Ctrl+B"
        needsSelection: true
        onActivated: canvas.openBiomePicker()
    }
    MapShortcut {
        sequence: "Ctrl+T"
        onActivated: canvas.openScreenshotDialog()
    }
    MapShortcut {
        sequence: "Ctrl+H"
        onActivated: workspace.toggleVoxelView()
    }
    MapShortcut {
        sequence: "Ctrl+G"
        onActivated: workspace.focusGotoFields()
    }

    Connections {
        target: workspace.session
        function onWorkspaceSectionChanged() {
            restoringSection = true
            section = session ? session.workspaceSection : 0
            restoringSection = false
        }
    }

    function setMapToggle(toggle, enabled) {
        if (toggle && toggle.checked !== enabled)
            toggle.checked = enabled
    }
    function setMapDimension(index) {
        if (mapController)
            mapController.setDimension(index)
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
    function setMapLayer(index) {
        if (mapController)
            mapController.setLayer(index)
    }
    function setMapSelectionMode(index) {
        if (mapController)
            mapController.setSelectionMode(index)
    }
    function setGridEnabled(enabled) {
        setMapToggle(gridToggle, enabled)
    }
    function toggleGrid() {
        setGridEnabled(!gridToggle.checked)
    }
    function setCoordinatesEnabled(enabled) {
        setMapToggle(coordsToggle, enabled)
    }
    function toggleCoordinates() {
        setCoordinatesEnabled(!coordsToggle.checked)
    }
    function setSlimeChunksEnabled(enabled) {
        setMapToggle(slimeToggle, enabled)
    }
    function toggleSlimeChunks() {
        setSlimeChunksEnabled(!slimeToggle.checked)
    }
    function setEntitiesEnabled(enabled) {
        setMapToggle(entitiesToggle, enabled)
    }
    function toggleEntities() {
        setEntitiesEnabled(!entitiesToggle.checked)
    }
    function setVillagesEnabled(enabled) {
        setMapToggle(villagesToggle, enabled)
    }
    function toggleVillages() {
        setVillagesEnabled(!villagesToggle.checked)
    }
    function setHsaEnabled(enabled) {
        setMapToggle(hsaToggle, enabled)
    }
    function toggleHsa() {
        setHsaEnabled(!hsaToggle.checked)
    }
    function setTransparentVoidEnabled(enabled) {
        if (mapController && mapController.transparentVoid !== enabled)
            mapController.setTransparentVoid(enabled)
    }
    function toggleTransparentVoid() {
        if (mapController)
            setTransparentVoidEnabled(!mapController.transparentVoid)
    }
    function toggleLayerPanel() {
        const opening = !layerPanelOpen
        layerPanelOpen = opening
        if (opening)
            mapFilterPanelOpen = false
    }
    function toggleMapFilterPanel() {
        const opening = !mapFilterPanelOpen
        mapFilterPanelOpen = opening
        if (opening)
            layerPanelOpen = false
    }
    function toggleDebugPanels() {
        debugVisible = !debugVisible
    }
    function toggleSelectionTool() {
        canvas.selectionToolActive = !canvas.selectionToolActive
    }
    function focusGotoFields() {
        xField.forceActiveFocus()
        xField.selectAll()
    }
    function jumpToCoordinates() {
        if (!mapController || xField.text.length === 0 || zField.text.length === 0)
            return
        const x = Number(xField.text)
        const z = Number(zField.text)
        if (!Number.isFinite(x) || !Number.isFinite(z))
            return
        mapController.gotoBlock(Math.round(x), Math.round(z))
    }
    function openChunkEditor(chunkX, chunkZ, dimension) {
        if (!session || !session.chunkEditor)
            return
        session.chunkEditor.openChunk(chunkX, chunkZ, dimension)
        canvas.selectionToolActive = false
        childPages.open("chunk-editor")
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
        openVoxelView()
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

    function restoreSessionState() {
        restoringSection = true
        section = session ? session.workspaceSection : 0
        restoringSection = false
        const route = session ? session.workspaceRoute : "root"
        childPages.reset(childPages.routeDefinition(route) ? route : "root")
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
                showGrid: gridToggle.checked
                showCoordinates: coordsToggle.checked
                showEntities: entitiesToggle.checked
                showSlimeChunks: slimeToggle.checked
                showVillages: villagesToggle.checked
                showHsa: hsaToggle.checked
                showRenderDebug: workspace.debugVisible && App.runtimeSettings.showMapDebugButton
                onChunkEditorRequested: (chunkX, chunkZ, dimension) => workspace.openChunkEditor(chunkX, chunkZ, dimension)
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
                Rectangle {
                    anchors.centerIn: parent
                    width: Theme.ui(3)
                    height: Math.min(56, parent.height * 0.25)
                    radius: width / 2
                    color: splitterMouse.pressed || splitterMouse.containsMouse ? Theme.accent : Theme.muted
                    opacity: 0.75
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

            AppCard {
                id: layerPanel
                visible: workspace.section === 0 && workspace.layerPanelOpen
                z: 2
                width: Math.max(0, Math.min(360, parent.width - Theme.gapMedium * 2))
                readonly property real naturalHeight: inspectorColumn.height + Theme.gapLarge * 2
                height: Math.min(naturalHeight, workspace.maximumInspectorPanelHeight())
                anchors.bottom: layerToolbar.top
                anchors.bottomMargin: Theme.gapSmall
                anchors.left: layerToolbar.left
                Flickable {
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: inspectorColumn.height + Theme.gapLarge * 2
                    Column {
                        id: inspectorColumn
                        x: Theme.gapLarge
                        y: Theme.gapLarge
                        width: parent.width - Theme.gapLarge * 2
                        spacing: Theme.gapSmall
                        Flow {
                            width: parent.width
                            spacing: Theme.gapSmall
                            AppToggle {
                                id: gridToggle
                                text: qsTr("Grid")
                                checked: true
                                onToggled: workspace.setGridEnabled(checked)
                            }
                            AppToggle {
                                id: coordsToggle
                                text: qsTr("Coords")
                                checked: false
                                onToggled: workspace.setCoordinatesEnabled(checked)
                            }
                            AppToggle {
                                id: transparentVoidToggle
                                text: qsTr("Transparent void")
                                enabled: !!workspace.mapController
                                onToggled: workspace.setTransparentVoidEnabled(checked)
                                Binding {
                                    target: transparentVoidToggle
                                    property: "checked"
                                    value: !!workspace.mapController && workspace.mapController.transparentVoid
                                }
                            }
                        }
                        Text {
                            text: qsTr("Dimension")
                            color: Theme.muted
                            font.pixelSize: Theme.fontCaption
                        }
                        Row {
                            width: parent.width
                            height: Theme.buttonHeight
                            spacing: Theme.gapSmall
                            AppSegmentedControl {
                                id: dimensionBar
                                width: Math.max(0, parent.width - customDimensionButton.width - parent.spacing)
                                height: parent.height
                                options: [qsTr("Overworld"), qsTr("Nether"), qsTr("The End")]
                                currentIndex: workspace.mapController && workspace.mapController.dimension >= 0 &&
                                              workspace.mapController.dimension < 3 ? workspace.mapController.dimension : -1
                                onActivated: index => {
                                    workspace.setMapDimension(index)
                                }
                            }
                            AppButton {
                                id: customDimensionButton
                                text: qsTr("Custom")
                                compact: workspace.compact
                                height: parent.height
                                active: !!workspace.mapController && (workspace.mapController.dimension < 0 ||
                                                                      workspace.mapController.dimension > 2)
                                onClicked: workspace.openCustomDimensionDialog()
                            }
                        }
                        Text {
                            text: qsTr("Layer")
                            color: Theme.muted
                            font.pixelSize: Theme.fontCaption
                        }
                        AppSegmentedControl {
                            id: layerBar
                            width: parent.width
                            height: Theme.buttonHeight
                            options: [qsTr("Terrain"), qsTr("Biomes")]
                            currentIndex: workspace.mapController ? workspace.mapController.layer : 0
                            onActivated: index => {
                                workspace.setMapLayer(index)
                            }
                        }
                        Text {
                            text: qsTr("Overlays")
                            color: Theme.muted
                            font.pixelSize: Theme.fontCaption
                        }
                        Flow {
                            width: parent.width
                            spacing: Theme.gapSmall
                            AppToggle {
                                id: slimeToggle
                                text: qsTr("Slime")
                                checked: false
                                onToggled: workspace.setSlimeChunksEnabled(checked)
                            }
                            AppToggle {
                                id: entitiesToggle
                                text: qsTr("Entities")
                                checked: false
                                onToggled: workspace.setEntitiesEnabled(checked)
                            }
                            AppToggle {
                                id: villagesToggle
                                text: qsTr("Villages")
                                onToggled: workspace.setVillagesEnabled(checked)
                            }
                            AppToggle {
                                id: hsaToggle
                                text: qsTr("HSA")
                                onToggled: workspace.setHsaEnabled(checked)
                            }
                        }
                        Text {
                            text: qsTr("Selection")
                            color: Theme.muted
                            font.pixelSize: Theme.fontCaption
                        }
                        AppSegmentedControl {
                            id: selectionModeControl
                            width: parent.width
                            height: Theme.buttonHeight
                            options: [qsTr("Replace"), qsTr("Add"), qsTr("Subtract")]
                            currentIndex: workspace.mapController ? workspace.mapController.selectionMode : 0
                            onActivated: index => {
                                workspace.setMapSelectionMode(index)
                            }
                        }
                    }
                }
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
                        onAccepted: workspace.jumpToCoordinates()
                    }
                    AppTextField {
                        id: zField
                        width: workspace.compact ? 72 : 96
                        placeholderText: "Z"
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        validator: IntValidator {}
                        anchors.verticalCenter: parent.verticalCenter
                        onAccepted: workspace.jumpToCoordinates()
                    }
                }
            }

            MapFilterPanel {
                id: mapFilterPanel
                visible: workspace.section === 0 && workspace.mapFilterPanelOpen
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
                                                                workspace.mapController
                                                                ? workspace.mapController.selectionChunkCount : 0) :
                                                            qsTr("Selection mode")
                        selected: canvas.selectionToolActive
                        onClicked: workspace.toggleSelectionTool()
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

    Rectangle {
        id: desktopSidebar
        visible: !workspace.compact
        z: 1
        width: Theme.sidebarCollapsedWidth
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        color: Theme.surface
        border.width: 0
        Rectangle {
            width: Theme.borderWidth
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            color: Theme.border
        }
        Flickable {
            id: desktopSidebarScroll
            width: parent.width
            anchors.top: parent.top
            anchors.topMargin: Theme.gapMedium
            anchors.bottom: desktopSidebarFooter.top
            contentWidth: width
            contentHeight: desktopSidebarColumn.height + Theme.gapMedium * 2
            clip: true
            Column {
                id: desktopSidebarColumn
                x: Theme.gapMedium
                y: Theme.gapMedium
                width: parent.width - Theme.gapMedium * 2
                spacing: Theme.gapTiny
                AppListRow {
                    iconName: "map"
                    label: qsTr("Map")
                    compact: true
                    selected: workspace.section === 0
                    onClicked: workspace.section = 0
                }
                AppListRow {
                    iconName: "nbt_editor"
                    label: qsTr("Chunk editor")
                    dirty: !!workspace.session && workspace.session.chunkEditor.dirty
                    compact: true
                    selected: workspace.chunkEditorOpen
                    onClicked: workspace.toggleChunkEditor()
                }
                AppListRow {
                    iconName: "structure"
                    label: qsTr("3D view")
                    dirty: !!workspace.session && workspace.session.voxelView.dirty
                    compact: true
                    selected: workspace.voxelViewOpen
                    onClicked: workspace.toggleVoxelView()
                }
                Rectangle {
                    width: parent.width
                    height: Theme.borderWidth
                    color: Theme.border
                }
                Repeater {
                    model: workspace.globalSections
                    delegate: AppListRow {
                        required property int index
                        required property var modelData
                        iconName: modelData.iconName
                        icon: modelData.shortLabel
                        dirty: modelData.dirty
                        label: modelData.title
                        compact: true
                        selected: workspace.section === index + 1
                        onClicked: workspace.section = index + 1
                    }
                }
            }
        }
        Rectangle {
            id: desktopSidebarFooterLine
            width: parent.width
            height: Theme.borderWidth
            anchors.bottom: desktopSidebarFooter.top
            color: Theme.border
        }
        Column {
            id: desktopSidebarFooter
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.gapMedium
            spacing: Theme.gapSmall
            AppIconButton {
                x: (parent.width - width) / 2
                iconName: workspace.session && !workspace.session.saving ? "save" : ""
                iconText: workspace.session && workspace.session.saving ? "…" : ""
                label: workspace.session && workspace.session.saving ? qsTr("Saving…") : qsTr("Save world")
                dirty: !!workspace.session && workspace.session.dirty
                visible: !!workspace.session
                enabled: workspace.session && workspace.session.canSave
                onClicked: workspace.saveRequested()
            }
            AppIconButton {
                x: (parent.width - width) / 2
                iconName: "exit"
                label: qsTr("Close world")
                danger: true
                onClicked: workspace.closeRequested()
            }
        }
    }

    AppCard {
        id: workspaceToolbar
        z: 7
        visible: App.runtimeSettings.showMapDebugButton && workspace.section === 0 && !workspace.chunkEditorOpen &&
                 !workspace.voxelViewOpen
        readonly property real toolbarPadding: Theme.gapSmall
        width: workspaceToolbarRow.width + toolbarPadding * 2
        height: workspaceToolbarRow.height + toolbarPadding * 2
        anchors.top: parent.top
        anchors.right: workspace.compact ? parent.right : desktopSidebar.left
        anchors.margins: Theme.gapLarge
        Row {
            id: workspaceToolbarRow
            x: workspaceToolbar.toolbarPadding
            height: Theme.touch ? Theme.controlHeight : 34
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.gapTiny
            AppIconButton {
                iconName: "debug"
                label: qsTr("Debug panels")
                selected: workspace.debugVisible
                onClicked: workspace.toggleDebugPanels()
            }
        }
    }

    AppBusyOverlay {
        anchors.fill: parent
        z: 40
        active: !!workspace.session && workspace.session.saving
        text: qsTr("Saving world…")
    }

    CustomDimensionDialog {
        id: customDimensionDialog
        onDimensionAccepted: dimension => workspace.setMapDimension(dimension)
    }
}

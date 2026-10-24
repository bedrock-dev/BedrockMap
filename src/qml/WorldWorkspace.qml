import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import "controls"

Item {
    id: workspace
    property var session
    property var mapController: session ? session.mapController : null
    property var terrainData: session ? session.terrainData : null
    property var globalSections: session && session.globalData ? session.globalData.sections : []
    property int section: 0
    property bool restoringSection: false
    property bool compact: Theme.compact
    property bool layerPanelOpen: false
    property bool debugVisible: false
    property bool chunkEditorOpen: false
    property bool voxelViewOpen: false
    readonly property bool childPageOpen: chunkEditorOpen || voxelViewOpen
    readonly property string childPageTitle: chunkEditorOpen ? chunkEditorPanel.headerTitle : (voxelViewOpen
                                                                                               ? voxelOverviewPanel.headerTitle :
                                                                                                 "")
    property real sidePanelWidth: 0
    signal closeRequested

    Connections {
        target: workspace.session
        function onSaved() {
            Notifications.info(qsTr("World saved"))
        }
        function onSaveFailed(message) {
            Notifications.error(qsTr("Could not save world"), "", message)
        }
        function onWorkspaceSectionChanged() {
            restoringSection = true
            section = session ? session.workspaceSection : 0
            restoringSection = false
        }
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
    function toggleChunkEditor() {
        if (chunkEditorOpen) {
            chunkEditorOpen = false
            return
        }
        canvas.selectionToolActive = false
        voxelViewOpen = false
        chunkEditorOpen = true
    }
    function openVoxelView() {
        if (!mapController || !session || !session.voxelView)
            return
        const area = mapController.voxelViewArea()
        if (!area || area.minX === undefined)
            return
        session.voxelView.openArea(area.minX, area.minZ, area.maxX, area.maxZ, area.dimension)
        canvas.selectionToolActive = false
        chunkEditorOpen = false
        voxelViewOpen = true
    }
    function toggleVoxelView() {
        if (voxelViewOpen) {
            voxelViewOpen = false
            return
        }
        if (session && session.voxelView && session.voxelView.voxelRevision > 0) {
            canvas.selectionToolActive = false
            chunkEditorOpen = false
            voxelViewOpen = true
        }
    }
    function clampSidePanelWidth(value) {
        const available = Math.max(1, sectionHost.width)
        return Math.max(280, Math.min(value, Math.max(280, available - 180)))
    }
    function currentSidePanelWidth() {
        return sidePanelWidth > 0 ? clampSidePanelWidth(sidePanelWidth) : sectionHost.width * 0.5
    }
    function goBack() {
        if (mapController && (mapController.regionImport.active || mapController.regionImport.loading)) {
            mapController.cancelImport()
            return true
        }
        if (chunkEditorOpen) {
            chunkEditorOpen = false
            return true
        }
        if (voxelViewOpen) {
            voxelViewOpen = false
            return true
        }
        return false
    }

    onSessionChanged: {
        restoringSection = true
        section = session ? session.workspaceSection : 0
        restoringSection = false
        chunkEditorOpen = false
        voxelViewOpen = false
        layerPanelOpen = false
    }
    onGlobalSectionsChanged: if (session && section >= globalSections.length + 1)
                                 section = 0
    onSectionChanged: {
        if (section !== 0 && canvas)
            canvas.selectionToolActive = false
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
                showGrid: gridToggle.active
                showCoordinates: coordsToggle.active
                showEntities: entitiesToggle.active
                showSlimeChunks: slimeToggle.active
                showVillages: villagesToggle.active
                showHsa: hsaToggle.active
                showRenderDebug: workspace.debugVisible
                onChunkEditorRequested: function (chunkX, chunkZ, dimension) {
                    if (workspace.session && workspace.session.chunkEditor)
                        workspace.session.chunkEditor.openChunk(chunkX, chunkZ, dimension)
                    workspace.chunkEditorOpen = true
                }
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
                    onCloseRequested: workspace.chunkEditorOpen = false
                }

                VoxelOverviewPanel {
                    id: voxelOverviewPanel
                    anchors.fill: parent
                    visible: workspace.voxelViewOpen
                    headerVisible: !workspace.compact
                    z: 1
                    controller: workspace.session ? workspace.session.voxelView : null
                    onCloseRequested: workspace.voxelViewOpen = false
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
                    width: 3
                    height: parent.height
                    radius: 1
                    color: splitterMouse.pressed || splitterMouse.containsMouse ? Theme.accent : Theme.border
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 3
                    height: Math.min(56, parent.height * 0.25)
                    radius: 2
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
                width: Math.min(260, Math.max(180, parent.width - Theme.gapLarge * 2))
                readonly property real naturalHeight: inspectorColumn.height + Theme.cardPadding * 2
                height: Math.min(naturalHeight, Math.max(0, layerToolbar.y - navigationToolbar.y - navigationToolbar.height
                                                         - Theme.gapSmall * 2))
                anchors.bottom: layerToolbar.top
                anchors.bottomMargin: Theme.gapSmall
                anchors.left: layerToolbar.left
                Flickable {
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: inspectorColumn.height + Theme.cardPadding * 2
                    Column {
                        id: inspectorColumn
                        x: Theme.cardPadding
                        y: Theme.cardPadding
                        width: parent.width - Theme.cardPadding * 2
                        spacing: Theme.gapSmall
                        Text {
                            text: qsTr("View")
                            color: Theme.muted
                            font.pixelSize: Theme.fontCaption
                        }
                        Flow {
                            width: parent.width
                            spacing: Theme.gapSmall
                            AppToggle {
                                id: gridToggle
                                label: qsTr("Grid")
                                active: true
                                compact: true
                            }
                            AppToggle {
                                id: coordsToggle
                                label: qsTr("Coords")
                                active: false
                                compact: true
                            }
                            AppToggle {
                                id: transparentVoidToggle
                                label: qsTr("Transparent void")
                                compact: true
                                enabled: !!workspace.mapController
                                onToggled: if (workspace.mapController)
                                               workspace.mapController.setTransparentVoid(active)
                                Binding {
                                    target: transparentVoidToggle
                                    property: "active"
                                    value: !!workspace.mapController && workspace.mapController.transparentVoid
                                }
                            }
                        }
                        Text {
                            text: qsTr("Dimension")
                            color: Theme.muted
                            font.pixelSize: Theme.fontCaption
                        }
                        AppSegmentedControl {
                            id: dimensionBar
                            width: parent.width
                            height: Theme.touch ? Theme.controlHeight + 6 : 28
                            options: [qsTr("Overworld"), qsTr("Nether"), qsTr("The End")]
                            currentIndex: workspace.mapController ? workspace.mapController.dimension : 0
                            onActivated: index => {
                                if (workspace.mapController)
                                    workspace.mapController.setDimension(index)
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
                            height: Theme.touch ? Theme.controlHeight + 6 : 28
                            options: [qsTr("Terrain"), qsTr("Biomes")]
                            currentIndex: workspace.mapController ? workspace.mapController.layer : 0
                            onActivated: index => {
                                if (workspace.mapController)
                                    workspace.mapController.setLayer(index)
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
                                label: qsTr("Slime")
                                active: false
                                compact: true
                            }
                            AppToggle {
                                id: entitiesToggle
                                label: qsTr("Entities")
                                active: false
                                compact: true
                            }
                            AppToggle {
                                id: villagesToggle
                                label: qsTr("Villages")
                                compact: true
                            }
                            AppToggle {
                                id: hsaToggle
                                label: qsTr("HSA")
                                compact: true
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
                            height: Theme.touch ? Theme.controlHeight + 6 : 28
                            options: [qsTr("Replace"), qsTr("Add"), qsTr("Subtract")]
                            currentIndex: workspace.mapController ? workspace.mapController.selectionMode : 0
                            onActivated: index => {
                                if (workspace.mapController)
                                    workspace.mapController.setSelectionMode(index)
                            }
                        }
                    }
                }
            }

            AppCard {
                id: navigationToolbar
                z: 1
                readonly property real horizontalPadding: workspace.compact ? 8 : 10
                readonly property real verticalPadding: Theme.touch ? 4 : 3
                width: navigationRow.width + horizontalPadding * 2
                height: navigationRow.height + verticalPadding * 2
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: Theme.gapLarge
                Row {
                    id: navigationRow
                    x: navigationToolbar.horizontalPadding
                    height: Theme.touch ? Theme.touchTarget : 34
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    AppIconButton {
                        iconName: "zoom_in"
                        label: qsTr("Zoom in")
                        buttonSize: 34
                        onClicked: canvas.zoomIn()
                    }
                    AppIconButton {
                        iconName: "zoom_out"
                        label: qsTr("Zoom out")
                        buttonSize: 34
                        onClicked: canvas.zoomOut()
                    }
                    AppIconButton {
                        iconName: "locate"
                        label: qsTr("Return to origin")
                        buttonSize: 34
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

            AppCard {
                id: layerToolbar
                z: 3
                readonly property real toolbarPadding: Theme.touch ? 4 : 3
                width: layerButton.width + toolbarPadding * 2
                height: layerButton.height + toolbarPadding * 2
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: Theme.gapLarge

                AppIconButton {
                    id: layerButton
                    anchors.centerIn: parent
                    iconName: "layers"
                    label: qsTr("Layers")
                    selected: workspace.layerPanelOpen
                    buttonSize: 34
                    onClicked: workspace.layerPanelOpen = !workspace.layerPanelOpen
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
        width: 56
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        color: Theme.surface
        border.width: 0
        Rectangle {
            width: 1
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            color: Theme.border
        }
        Flickable {
            id: desktopSidebarScroll
            width: parent.width
            anchors.top: parent.top
            anchors.topMargin: Theme.cardPadding
            anchors.bottom: desktopSidebarFooter.top
            contentWidth: width
            contentHeight: desktopSidebarColumn.height + Theme.cardPadding * 2
            clip: true
            Column {
                id: desktopSidebarColumn
                x: Theme.cardPadding
                y: Theme.cardPadding
                width: parent.width - Theme.cardPadding * 2
                spacing: 2
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
                    height: 1
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
            height: 1
            anchors.bottom: desktopSidebarFooter.top
            color: Theme.border
        }
        Column {
            id: desktopSidebarFooter
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.cardPadding
            spacing: Theme.gapSmall
            AppIconButton {
                x: (parent.width - width) / 2
                iconName: workspace.session && !workspace.session.saving ? "save" : ""
                icon: workspace.session && workspace.session.saving ? "…" : ""
                label: workspace.session && workspace.session.saving ? qsTr("Saving…") : qsTr("Save world")
                dirty: !!workspace.session && workspace.session.dirty
                buttonSize: 34
                visible: !!workspace.session
                enabled: workspace.session && workspace.session.canSave
                onClicked: {
                    if (!workspace.session.commit())
                        Notifications.warning(qsTr("Wait for the current map operation to finish before saving."))
                }
            }
            AppIconButton {
                x: (parent.width - width) / 2
                iconName: "exit"
                label: qsTr("Close world")
                buttonSize: 34
                danger: true
                onClicked: workspace.closeRequested()
            }
        }
    }

    AppCard {
        id: workspaceToolbar
        z: 7
        visible: workspace.section === 0 && !workspace.chunkEditorOpen && !workspace.voxelViewOpen
        readonly property real toolbarPadding: Theme.touch ? 4 : 3
        width: workspaceToolbarRow.width + toolbarPadding * 2
        height: workspaceToolbarRow.height + toolbarPadding * 2
        anchors.top: parent.top
        anchors.right: workspace.compact ? parent.right : desktopSidebar.left
        anchors.margins: Theme.gapLarge
        Row {
            id: workspaceToolbarRow
            x: workspaceToolbar.toolbarPadding
            height: Theme.touch ? Theme.touchTarget : 34
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            AppIconButton {
                visible: Theme.touch
                enabled: !canvas.importActive && !canvas.inputBlocked
                icon: "▣"
                label: canvas.selectionToolActive ? qsTr("Exit selection mode (%1 chunks)").arg(workspace.mapController
                                                                                                ? workspace.mapController.selectionChunkCount :
                                                                                                  0) : qsTr(
                                                        "Selection mode")
                selected: canvas.selectionToolActive
                buttonSize: 34
                onClicked: canvas.selectionToolActive = !canvas.selectionToolActive
            }
            AppIconButton {
                iconName: "debug"
                label: qsTr("Debug panels")
                selected: workspace.debugVisible
                buttonSize: 34
                onClicked: workspace.debugVisible = !workspace.debugVisible
            }
        }
    }

    AppBusyOverlay {
        anchors.fill: parent
        z: 40
        active: !!workspace.session && workspace.session.saving
        text: qsTr("Saving world…")
    }
}

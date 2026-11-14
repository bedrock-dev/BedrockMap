pragma ComponentBehavior: Bound

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

Item {
    id: root

    property var controller: null
    property int editingIndex: -1
    property var hiddenIndices: []
    property bool syncingSelection: false
    readonly property int iconButtonSize: 28
    readonly property int editButtonWidth: 60
    readonly property int deleteColumnWidth: 40
    readonly property var hsaAreas: controller ? controller.hsaAreas : []
    readonly property var hsaTypes: controller ? controller.hsaTypes : []
    readonly property var hsaTypeValues: hsaTypes.map(item => item.value)
    readonly property var hsaTypeNames: hsaTypes.map(item => item.name)
    readonly property var renderHsaAreas: {
        const result = []
        const origin = viewport.camera.modelOrigin
        for (const area of root.hsaAreas) {
            const index = Number(area.index)
            if (root.hiddenIndices.indexOf(index) >= 0)
                continue
            result.push({
                            minX: Number(area.minX) - origin.x,
                            minY: Number(area.minY) - origin.y,
                            minZ: Number(area.minZ) - origin.z,
                            maxX: Number(area.maxX) - origin.x + 1,
                            maxY: Number(area.maxY) - origin.y + 1,
                            maxZ: Number(area.maxZ) - origin.z + 1,
                            color: area.color
                        })
        }
        return result
    }

    function coordinateText(x, y, z) {
        return x + ", " + y + ", " + z
    }

    function hsaTypeIndex(type) {
        return root.hsaTypeValues.indexOf(Number(type))
    }

    function setHsaVisible(index, visible) {
        if (!visible && root.editingIndex === index)
            root.stopEditing()
        const next = root.hiddenIndices.filter(value => value !== index)
        if (!visible)
            next.push(index)
        root.hiddenIndices = next
    }

    function addHsa() {
        if (!root.controller || !root.controller.hasVoxelModel)
            return
        const origin = viewport.camera.modelOrigin
        const size = viewport.camera.modelSize
        const index = root.controller.addHsa(root.hsaTypeValues[0], Math.round(origin.x), Math.round(origin.y),
                                             Math.round(origin.z), Math.round(origin.x + size.x) - 1,
                                             Math.round(origin.y + size.y) - 1, Math.round(origin.z + size.z) - 1)
        if (index < 0)
            return
        root.hiddenIndices = root.hiddenIndices.filter(value => value !== index)
        Qt.callLater(() => areaList.positionViewAtEnd())
    }

    function deleteHsa(index) {
        if (!root.controller || !root.hsaAreas[index])
            return
        if (root.editingIndex === index)
            root.stopEditing()
        else if (root.editingIndex > index)
            root.editingIndex -= 1
        root.hiddenIndices = root.hiddenIndices.filter(value => value !== index).map(value => value > index ? value - 1 : value)
        root.controller.removeHsa(index)
    }

    function startEditing(index) {
        if (!root.controller || !root.controller.hasVoxelModel || !root.hsaAreas[index])
            return
        if (root.editingIndex === index) {
            root.stopEditing()
            return
        }
        if (root.hiddenIndices.indexOf(index) >= 0)
            root.setHsaVisible(index, true)
        root.editingIndex = index

        const area = root.hsaAreas[index]
        const origin = viewport.camera.modelOrigin
        root.syncingSelection = true
        viewport.camera.selectionMoveMode = false
        viewport.camera.selectionEnabled = true
        viewport.camera.setSelectionBounds(Qt.vector3d(Number(area.minX) - origin.x, Number(area.minY) - origin.y,
                                                       Number(area.minZ) - origin.z),
                                           Qt.vector3d(Number(area.maxX) - origin.x + 1, Number(area.maxY) - origin.y + 1,
                                                       Number(area.maxZ) - origin.z + 1), true)
        root.syncingSelection = false
    }

    function stopEditing() {
        root.editingIndex = -1
        root.syncingSelection = true
        viewport.camera.selectionEnabled = false
        root.syncingSelection = false
    }

    function resetEditorState() {
        root.stopEditing()
        root.hiddenIndices = []
    }

    function pruneIndices() {
        const count = root.hsaAreas.length
        const hidden = root.hiddenIndices.filter(index => index < count)
        if (hidden.length !== root.hiddenIndices.length)
            root.hiddenIndices = hidden
        if (root.editingIndex >= count)
            root.stopEditing()
    }

    onControllerChanged: resetEditorState()
    onVisibleChanged: if (!visible)
                          stopEditing()

    Connections {
        target: root.controller
        function onChunkChanged() {
            root.resetEditorState()
        }
        function onVoxelChanged() {
            root.stopEditing()
        }
        function onHsaChanged() {
            root.pruneIndices()
        }
    }

    // Commit bounds only when the selection settles: committing every drag frame
    // would rebuild hsaAreas and reset the list model on each mouse move.
    Connections {
        target: viewport.camera
        function onSelectionChanged() {
            if (!viewport.camera.selectionDragging)
                root.commitEditingBounds()
        }
        function onSelectionInteractionChanged() {
            if (!viewport.camera.selectionDragging)
                root.commitEditingBounds()
        }
    }

    function commitEditingBounds() {
        if (root.syncingSelection || root.editingIndex < 0 || !viewport.camera.selectionEnabled)
            return
        if (!root.hsaAreas[root.editingIndex])
            return
        const origin = viewport.camera.modelOrigin
        const minimum = viewport.camera.selectionMinimum
        const maximum = viewport.camera.selectionMaximum
        root.controller.setHsaBounds(root.editingIndex, Math.round(minimum.x + origin.x), Math.round(minimum.y + origin.y),
                                     Math.round(minimum.z + origin.z), Math.round(maximum.x + origin.x - 1),
                                     Math.round(maximum.y + origin.y - 1), Math.round(maximum.z + origin.z - 1))
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical

        handle: AppSplitHandle {
            orientation: SplitView.view.orientation
        }

        VoxelView {
            id: viewport
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            SplitView.minimumHeight: Theme.ui(180)
            SplitView.preferredHeight: Math.max(Theme.ui(180), root.height * 0.60)
            controller: root.controller
            hsaAreas: root.renderHsaAreas
        }

        AppCard {
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            SplitView.minimumHeight: Theme.ui(120)
            SplitView.preferredHeight: Math.max(Theme.ui(120), root.height * 0.40)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.gapLarge
                spacing: Theme.gapSmall

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.gapMedium
                        anchors.rightMargin: Theme.gapMedium
                        spacing: Theme.gapMedium

                        Text {
                            Layout.preferredWidth: 120
                            text: qsTr("Type")
                            color: Theme.muted
                            font.pixelSize: Theme.fontCaption
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Range")
                            color: Theme.muted
                            font.pixelSize: Theme.fontCaption
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.preferredWidth: root.deleteColumnWidth
                            text: qsTr("Delete")
                            color: Theme.muted
                            font.pixelSize: Theme.fontCaption
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }

                ListView {
                    id: areaList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: root.hsaAreas
                    currentIndex: -1
                    keyNavigationEnabled: false
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    spacing: 0
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Item {
                        id: areaRow
                        required property var modelData
                        readonly property int areaIndex: Number(modelData.index)
                        readonly property bool areaVisible: root.hiddenIndices.indexOf(areaIndex) < 0
                        readonly property bool editing: root.editingIndex === areaIndex

                        width: areaList.width
                        height: Math.max(38, Theme.controlHeight + Theme.gapSmall * 2)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.gapMedium
                            anchors.rightMargin: Theme.gapMedium
                            spacing: Theme.gapMedium

                            AppComboBox {
                                Layout.preferredWidth: 120
                                model: root.hsaTypeNames
                                currentIndex: root.hsaTypeIndex(areaRow.modelData.type)
                                enabled: root.controller && root.controller.hasChunk
                                onActivated: function (index) {
                                    root.controller.setHsaType(areaRow.areaIndex, root.hsaTypeValues[index])
                                }
                            }
                            // Range column: the bounds plus the controls that act on them.
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: Theme.gapSmall

                                Text {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: root.coordinateText(areaRow.modelData.minX, areaRow.modelData.minY,
                                                              areaRow.modelData.minZ) + "  ~  "
                                          + root.coordinateText(areaRow.modelData.maxX, areaRow.modelData.maxY,
                                                                areaRow.modelData.maxZ)
                                    color: areaRow.areaVisible ? Theme.text : Theme.muted
                                    font.pixelSize: Theme.fontCaption
                                    elide: Text.ElideMiddle
                                    verticalAlignment: Text.AlignVCenter
                                }
                                AppIconButton {
                                    iconName: areaRow.areaVisible ? "visible" : "hidden"
                                    label: areaRow.areaVisible ? qsTr("Hide HSA") : qsTr("Show HSA")
                                    selected: areaRow.areaVisible
                                    buttonSize: root.iconButtonSize
                                    onClicked: root.setHsaVisible(areaRow.areaIndex, !areaRow.areaVisible)
                                }
                                AppButton {
                                    Layout.preferredWidth: root.editButtonWidth
                                    compact: true
                                    text: areaRow.editing ? qsTr("Editing") : qsTr("Edit")
                                    primary: areaRow.editing
                                    enabled: root.controller && root.controller.hasVoxelModel
                                    onClicked: root.startEditing(areaRow.areaIndex)
                                }
                            }
                            Item {
                                Layout.preferredWidth: root.deleteColumnWidth
                                Layout.fillHeight: true

                                AppIconButton {
                                    anchors.centerIn: parent
                                    iconName: "close"
                                    label: qsTr("Delete HSA")
                                    danger: true
                                    buttonSize: root.iconButtonSize
                                    onClicked: root.deleteHsa(areaRow.areaIndex)
                                }
                            }
                        }
                    }

                    footer: Item {
                        width: areaList.width
                        height: Math.max(38, Theme.controlHeight + Theme.gapSmall * 2)

                        AppButton {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.gapMedium
                            anchors.verticalCenter: parent.verticalCenter
                            width: 60
                            compact: true
                            text: qsTr("Add")
                            enabled: root.controller && root.controller.hasVoxelModel
                            onClicked: root.addHsa()
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: root.hsaAreas.length === 0 && !(root.controller && root.controller.loading)
                text: qsTr("No hardcoded spawn areas in this chunk.")
                color: Theme.muted
                font.pixelSize: Theme.fontBody
            }
        }
    }
}

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

Item {
    id: root
    property var controller
    property var contextCell: null
    property bool showGrid: false
    readonly property var terrainCells: controller ? controller.terrainCells : []

    function cellValue(kind) {
        if (!root.contextCell)
            return ""
        if (kind === "position")
            return root.contextCell.x + "," + root.contextCell.y + "," + root.contextCell.z
        if (kind === "block")
            return root.contextCell.blockName
        if (kind === "palette")
            return root.contextCell.blockPalette
        if (kind === "biome")
            return root.contextCell.biomeLabel
        return ""
    }

    function copyValue(kind) {
        if (root.controller && root.contextCell)
            root.controller.copyText(String(root.cellValue(kind)))
    }

    component CellMenuItem: AppMenuItem {
        id: entry
        required property string actionText
        required property string valueKind
        readonly property string value: String(root.cellValue(valueKind))
        text: actionText + ": " + value.replace(/\s+/g, " ").trim()
        onTriggered: root.copyValue(valueKind)

        AppToolTip {
            text: entry.actionText + ": " + entry.value
            shown: entry.hovered && entry.value.length > 0
            width: Math.min(implicitWidth, cellMenu.width)
        }
    }

    AppMenu {
        id: cellMenu
        width: Math.min(Math.max(0, root.width - 12), 640, Math.max(190, positionItem.implicitWidth,
                                                                    blockItem.implicitWidth, paletteItem.implicitWidth,
                                                                    biomeItem.implicitWidth) + leftPadding
                        + rightPadding)
        CellMenuItem {
            id: positionItem
            actionText: qsTr("Copy position")
            valueKind: "position"
        }
        CellMenuItem {
            id: blockItem
            actionText: qsTr("Copy block name")
            valueKind: "block"
        }
        CellMenuItem {
            id: paletteItem
            actionText: qsTr("Copy block palette")
            valueKind: "palette"
        }
        CellMenuItem {
            id: biomeItem
            actionText: qsTr("Copy biome")
            valueKind: "biome"
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gapSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapSmall
            enabled: root.controller && root.controller.hasChunk

            Text {
                text: qsTr("Y")
                color: Theme.muted
                font.pixelSize: Theme.fontCaption
            }
            AppSpinBox {
                id: ySpin
                Layout.preferredWidth: 96
                from: root.controller && root.controller.hasChunk ? root.controller.terrainMinY : 0
                to: root.controller && root.controller.hasChunk ? root.controller.terrainMaxY : 0
                onValueModified: if (root.controller)
                                     root.controller.terrainY = value
            }
            AppSlider {
                id: ySlider
                Layout.fillWidth: true
                from: root.controller && root.controller.hasChunk ? root.controller.terrainMinY : 0
                to: root.controller && root.controller.hasChunk ? root.controller.terrainMaxY : 0
                stepSize: 1
                onMoved: if (root.controller)
                             root.controller.terrainY = value
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapSmall
            enabled: root.controller && root.controller.hasChunk

            Text {
                text: qsTr("Layer")
                color: Theme.muted
                font.pixelSize: Theme.fontCaption
            }
            AppSpinBox {
                id: layerSpin
                Layout.preferredWidth: 96
                from: 0
                to: root.controller ? root.controller.terrainMaxLayer : 0
                onValueModified: if (root.controller)
                                     root.controller.terrainLayer = value
            }
            AppCheckBox {
                text: qsTr("Grid")
                checked: root.showGrid
                onToggled: root.showGrid = checked
            }
            Item {
                Layout.fillWidth: true
            }
        }

        Binding {
            target: ySpin
            property: "value"
            value: root.controller ? root.controller.terrainY : 0
            restoreMode: Binding.RestoreBindingOrValue
        }
        Binding {
            target: ySlider
            property: "value"
            value: root.controller ? root.controller.terrainY : 0
            restoreMode: Binding.RestoreBindingOrValue
        }
        Binding {
            target: layerSpin
            property: "value"
            value: root.controller ? root.controller.terrainLayer : 0
            restoreMode: Binding.RestoreBindingOrValue
        }

        Item {
            id: viewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Column {
                id: squares
                readonly property real edge: Math.max(0, Math.min(viewport.width, Math.floor((viewport.height
                                                                                              - spacing) / 2)))
                width: edge
                anchors.centerIn: viewport
                spacing: Theme.gapMedium

                TerrainSquare {
                    width: squares.edge
                    height: width
                }
                TerrainSquare {
                    width: squares.edge
                    height: width
                    biomeLayer: true
                }
            }
        }
    }

    component TerrainSquare: Rectangle {
        id: square
        property bool biomeLayer: false

        color: Theme.surfaceAlt
        border.width: Theme.borderWidth
        border.color: Theme.border
        clip: true

        Grid {
            id: grid
            anchors.fill: parent
            columns: 16
            rows: 16

            Repeater {
                model: root.terrainCells
                delegate: Rectangle {
                    required property int index
                    required property var modelData

                    width: grid.width / 16
                    height: grid.height / 16
                    color: square.biomeLayer ? modelData.biomeColor : modelData.terrainColor
                    border.width: root.showGrid ? Theme.borderWidth : 0
                    border.color: Theme.border

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        preventStealing: true
                        onClicked: function (mouse) {
                            if (mouse.button !== Qt.RightButton)
                                return
                            root.contextCell = modelData
                            const point = mapToItem(root, mouse.x, mouse.y)
                            cellMenu.popup(point.x, point.y)
                        }
                    }
                }
            }
        }
    }
}

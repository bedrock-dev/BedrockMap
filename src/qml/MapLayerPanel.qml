pragma ComponentBehavior: Bound

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import "controls"

// Dimension, layer, overlay and selection-mode options for one world's map.
AppCard {
    id: panel

    property var controller: null
    property var terrainData: null
    property bool compact: false
    property bool debugVisible: false
    property bool showDebugToggle: false
    readonly property var overlays: controller ? controller.overlays : ({})
    readonly property real naturalHeight: inspectorColumn.height + Theme.gapLarge * 2

    signal customDimensionRequested
    signal debugToggleRequested

    // Clicking a checkable button breaks a plain `checked` binding, so the
    // state is re-applied from the controller through a Binding instead.
    component OverlayToggle: AppToggle {
        id: toggle
        required property string overlay
        readonly property var overlayState: panel.overlays[toggle.overlay] || ({})
        enabled: !!panel.controller
        onToggled: {
            if (panel.controller && toggle.overlayState.type !== undefined)
            panel.controller.setOverlay(Number(toggle.overlayState.type), toggle.checked)
        }
        Binding {
            target: toggle
            property: "checked"
            value: !!toggle.overlayState.enabled
        }
    }

    component SectionLabel: Text {
        color: Theme.muted
        font.pixelSize: Theme.fontSmall
    }

    Flickable {
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: panel.naturalHeight
        ScrollBar.vertical: AppScrollBar {}
        Column {
            id: inspectorColumn
            x: Theme.gapLarge
            y: Theme.gapLarge
            width: parent.width - Theme.gapLarge * 2
            spacing: Theme.gapSmall
            Flow {
                width: parent.width
                spacing: Theme.gapSmall
                OverlayToggle {
                    overlay: "Grid"
                    text: qsTr("Grid")
                }
                OverlayToggle {
                    overlay: "Coords"
                    text: qsTr("Coords")
                }
                AppToggle {
                    id: transparentVoidToggle
                    text: qsTr("Transparent void")
                    enabled: !!panel.controller
                    onToggled: panel.controller.setTransparentVoid(checked)
                    Binding {
                        target: transparentVoidToggle
                        property: "checked"
                        value: !!panel.controller && panel.controller.transparentVoid
                    }
                }
                OverlayToggle {
                    overlay: "DimensionBorder"
                    text: qsTr("Dimension border")
                    enabled: !!panel.controller && !!panel.terrainData && panel.terrainData.chunkCoordsEnabled
                }
                OverlayToggle {
                    overlay: "TerrainOutline"
                    text: qsTr("Terrain outline")
                }
                AppToggle {
                    id: debugToggle
                    visible: panel.showDebugToggle
                    text: qsTr("Debug panels")
                    onToggled: panel.debugToggleRequested()
                    Binding {
                        target: debugToggle
                        property: "checked"
                        value: panel.debugVisible
                    }
                }
            }
            SectionLabel {
                text: qsTr("Dimension")
            }
            Row {
                width: parent.width
                height: Theme.buttonHeight
                spacing: Theme.gapSmall
                AppSegmentedControl {
                    width: Math.max(0, parent.width - customDimensionButton.width - parent.spacing)
                    height: parent.height
                    options: [qsTr("Overworld"), qsTr("Nether"), qsTr("The End")]
                    currentIndex: panel.controller && panel.controller.dimension >= 0 && panel.controller.dimension < 3
                                  ? panel.controller.dimension : -1
                    onActivated: index => {
                        if (panel.controller)
                            panel.controller.setDimension(index)
                    }
                }
                AppButton {
                    id: customDimensionButton
                    text: qsTr("Custom")
                    compact: panel.compact
                    height: parent.height
                    active: !!panel.controller && (panel.controller.dimension < 0 || panel.controller.dimension > 2)
                    onClicked: panel.customDimensionRequested()
                }
            }
            SectionLabel {
                text: qsTr("Layer")
            }
            AppSegmentedControl {
                width: parent.width
                height: Theme.buttonHeight
                options: [qsTr("Terrain"), qsTr("Biomes")]
                currentIndex: panel.controller ? panel.controller.layer : 0
                onActivated: index => {
                    if (panel.controller)
                        panel.controller.setLayer(index)
                }
            }
            SectionLabel {
                text: qsTr("Overlays")
            }
            Flow {
                width: parent.width
                spacing: Theme.gapSmall
                OverlayToggle {
                    overlay: "SlimeChunk"
                    text: qsTr("Slime")
                }
                OverlayToggle {
                    overlay: "Actors"
                    text: qsTr("Entities")
                }
                OverlayToggle {
                    overlay: "Village"
                    text: qsTr("Villages")
                }
                OverlayToggle {
                    overlay: "HSA"
                    text: qsTr("HSA")
                }
                OverlayToggle {
                    overlay: "Portal"
                    text: qsTr("Portal")
                }
            }
            SectionLabel {
                text: qsTr("Selection")
            }
            AppSegmentedControl {
                width: parent.width
                height: Theme.buttonHeight
                options: [qsTr("Replace"), qsTr("Add"), qsTr("Subtract")]
                currentIndex: panel.controller ? panel.controller.selectionMode : 0
                onActivated: index => {
                    if (panel.controller)
                        panel.controller.setSelectionMode(index)
                }
            }
        }
    }
}

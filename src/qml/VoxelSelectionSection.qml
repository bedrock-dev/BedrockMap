import BedrockMap 1.0
import QtQuick
import QtQuick.Layouts
import "controls"

// Selection toggles and the box bounds, shown in world coordinates.
VoxelPreviewSection {
    id: section

    required property var camera
    property bool importPreviewActive: false
    readonly property var worldOrigin: [camera.modelOrigin.x, camera.modelOrigin.y, camera.modelOrigin.z]

    function axisValue(vector, axis) {
        return axis === 0 ? vector.x : axis === 1 ? vector.y : vector.z
    }

    component SelectionCoordinate: AppSpinBox {
        required property int axis
        property bool upperBoundary: false
        readonly property int origin: section.worldOrigin[axis]
        readonly property int lower: section.axisValue(section.camera.selectionMinimum, axis)
        readonly property int upper: section.axisValue(section.camera.selectionMaximum, axis)
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 110
        from: origin + (upperBoundary ? lower + 1 : 0)
        to: Math.max(from, origin + (upperBoundary ? section.axisValue(section.camera.modelSize, axis) : upper - 1))
        value: Math.max(from, Math.min(to, origin + (upperBoundary ? upper : lower)))
        onValueModified: section.camera.setSelectionBoundary(axis, upperBoundary, value - origin)
        Accessible.name: (upperBoundary ? qsTr("Maximum (exclusive)") : qsTr("Minimum")) + " " + ["X", "Y", "Z"][axis]
    }

    component AxisCaption: Text {
        color: Theme.muted
        font.pixelSize: Theme.fontCaption
    }

    objectName: "voxelSelectionSection"
    title: qsTr("Selection")
    contentItem: ColumnLayout {
        spacing: Theme.gapSmall

        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: Theme.gapSmall

            VoxelPreviewCheckBox {
                objectName: "voxelSelectionToggle"
                text: qsTr("Enable selection")
                enabled: !section.importPreviewActive
                checked: section.camera.selectionEnabled
                onToggled: section.camera.selectionEnabled = checked
            }
            VoxelPreviewCheckBox {
                objectName: "voxelSelectionMoveToggle"
                text: qsTr("Move selection")
                enabled: section.camera.selectionEnabled && !section.importPreviewActive
                checked: section.camera.selectionMoveMode
                onToggled: section.camera.selectionMoveMode = checked
            }
        }

        GridLayout {
            objectName: "voxelSelectionCoordinates"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            enabled: section.camera.selectionEnabled && !section.importPreviewActive
            columns: 4
            columnSpacing: Theme.gapSmall
            rowSpacing: Theme.gapSmall

            Item {}
            Repeater {
                model: ["X", "Y", "Z"]
                delegate: AxisCaption {
                    required property string modelData
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 110
                    text: modelData
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            AxisCaption {
                text: qsTr("Start")
            }
            Repeater {
                model: 3
                delegate: SelectionCoordinate {
                    required property int index
                    axis: index
                    objectName: "voxelSelectionMin" + ["X", "Y", "Z"][axis]
                }
            }

            AxisCaption {
                text: qsTr("End")
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

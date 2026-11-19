import BedrockMap 1.0
import QtQuick
import QtQuick.Layouts
import "controls"

// Projection, rotation lock, axes and the rotation pad for one VoxelView.
VoxelPreviewSection {
    id: section

    required property var view
    readonly property var camera: view.camera

    component ViewButton: AppIconButton {
        buttonSize: Theme.iconButtonSmallSize
        Layout.preferredWidth: controlSize
        Layout.preferredHeight: controlSize
    }

    objectName: "voxelViewSection"
    title: qsTr("View")
    contentItem: RowLayout {
        objectName: "voxelViewControls"
        spacing: Theme.gapMedium

        ColumnLayout {
            objectName: "voxelViewToggles"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: Theme.gapSmall

            AppCheckBox {
                objectName: "voxelOrthoToggle"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                wrapText: true
                text: qsTr("Orthographic projection")
                checked: section.camera.orthoMode
                onToggled: section.camera.orthoMode = checked
            }
            AppCheckBox {
                objectName: "voxelRotationLockToggle"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                wrapText: true
                text: qsTr("Lock rotation")
                checked: section.camera.rotationLocked
                onToggled: section.camera.rotationLocked = checked
            }
            AppCheckBox {
                objectName: "voxelShowAxesToggle"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                wrapText: true
                text: qsTr("Show axes")
                checked: section.view.showAxes
                onToggled: section.view.showAxes = checked
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
                iconName: "caret_up"
                label: qsTr("Rotate up")
                onClicked: section.camera.rotateView(0, -90)
            }
            Item {}

            ViewButton {
                objectName: "voxelRotateLeft"
                iconName: "caret_left"
                label: qsTr("Rotate left")
                onClicked: section.camera.rotateView(-90, 0)
            }
            ViewButton {
                objectName: "voxelFaceFront"
                iconName: "front"
                label: qsTr("Face front")
                onClicked: section.camera.focusFrontFace()
            }
            ViewButton {
                objectName: "voxelRotateRight"
                iconName: "caret_right"
                label: qsTr("Rotate right")
                onClicked: section.camera.rotateView(90, 0)
            }

            Item {}
            ViewButton {
                objectName: "voxelRotateDown"
                iconName: "caret_down"
                label: qsTr("Rotate down")
                onClicked: section.camera.rotateView(0, 90)
            }
            Item {}
        }
    }
}

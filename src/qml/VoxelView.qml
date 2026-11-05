import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

Item {
    id: root
    property var controller: null
    property alias camera: voxelItem
    property alias showAxes: voxelItem.showAxes
    readonly property bool inputEnabled: !!root.controller && !root.controller.loading

    function endInteraction() {
        if (!voxelItem)
            return
        voxelItem.endSelectionDrag()
        voxelItem.endView()
    }

    Rectangle {
        anchors.fill: parent
        color: "#191e26"
    }

    VoxelQuickItem {
        id: voxelItem
        anchors.fill: parent
        controller: root.controller
        selectionColor: App.runtimeSettings.voxelSelectionColor
        saturation: App.runtimeSettings.gpuSaturation
        brightness: App.runtimeSettings.gpuBrightness
    }

    DragHandler {
        enabled: root.inputEnabled && touchInput.pointCount === 0 && !voxelItem.selectionDragging
        target: null
        acceptedDevices: PointerDevice.Mouse
        acceptedButtons: Qt.LeftButton
        onActiveChanged: {
            if (active)
                voxelItem.beginRotate(centroid.position)
            else
                voxelItem.endView()
        }
        onTranslationChanged: if (active)
                                  voxelItem.dragView(centroid.position)
    }

    DragHandler {
        enabled: root.inputEnabled && touchInput.pointCount === 0 && !voxelItem.selectionDragging
        target: null
        acceptedDevices: PointerDevice.Mouse
        acceptedButtons: Qt.RightButton
        onActiveChanged: {
            if (active)
                voxelItem.beginPan(centroid.position)
            else
                voxelItem.endView()
        }
        onTranslationChanged: if (active)
                                  voxelItem.dragView(centroid.position)
    }

    WheelHandler {
        enabled: root.inputEnabled
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function (wheel) {
            if (wheel.angleDelta.y !== 0)
                voxelItem.zoomBy(Math.pow(1.1, wheel.angleDelta.y / 120))
            wheel.accepted = true
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.inputEnabled && touchInput.pointCount === 0
        acceptedButtons: Qt.MiddleButton
        preventStealing: true
        cursorShape: voxelItem.selectionDragging ? (voxelItem.selectionMoveMode ? Qt.SizeAllCursor :
                                                                                  Qt.ClosedHandCursor) : Qt.ArrowCursor
        onPressed: function (mouse) {
            voxelItem.endView()
            mouse.accepted = voxelItem.beginSelectionDrag(Qt.point(mouse.x, mouse.y))
        }
        onPositionChanged: function (mouse) {
            if (pressed)
                voxelItem.dragSelection(Qt.point(mouse.x, mouse.y))
        }
        onReleased: voxelItem.endSelectionDrag()
        onCanceled: voxelItem.endSelectionDrag()
    }

    AppTouchGestureArea {
        id: touchInput
        anchors.fill: parent
        enabled: root.inputEnabled
        onContactStarted: {
            root.endInteraction()
            if (pointCount === 1)
                voxelItem.beginSelectionDrag(startPosition, 22)
        }
        onPointCountChanged: if (pointCount === 0)
                                 root.endInteraction()
        onManipulationStarted: function (points, position) {
            if (points === 1 && !voxelItem.selectionDragging)
                voxelItem.beginRotate(position)
            else if (points === 2)
                voxelItem.beginPan(position)
        }
        onManipulationUpdated: function (points, previous, position, scale) {
            if (points === 1 && voxelItem.selectionDragging)
                voxelItem.dragSelection(position)
            else
                voxelItem.dragView(position)
            if (points === 2)
                voxelItem.zoomBy(scale)
        }
        onManipulationFinished: root.endInteraction()
    }

    onControllerChanged: {
        if (touchInput)
            touchInput.cancelGesture()
        root.endInteraction()
    }
    onVisibleChanged: if (!visible) {
                          if (touchInput)
                              touchInput.cancelGesture()
                          root.endInteraction()
                      }

    Connections {
        target: root.controller
        function onVoxelChanged() {
            touchInput.cancelGesture()
            root.endInteraction()
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 56
        visible: root.controller && root.controller.loading
        color: "#b0101419"

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.topMargin: 8
            anchors.bottomMargin: 8
            spacing: 4

            Text {
                Layout.fillWidth: true
                text: root.controller ? qsTr("Building voxel terrain… %1%").arg(root.controller.voxelProgress) : qsTr(
                                            "Building voxel terrain…")
                color: "#f1f4f8"
                font.pixelSize: Theme.fontCaption
            }
            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: 100
                value: root.controller ? root.controller.voxelProgress : 0
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: root.controller && !root.controller.loading && !root.controller.hasVoxelModel
        text: root.controller && root.controller.errorMessage.length > 0 ? root.controller.errorMessage : qsTr(
                                                                               "No terrain loaded.")
        color: Theme.muted
        font.pixelSize: Theme.fontSubtitle
    }

    Text {
        objectName: "voxelViewportInfo"
        readonly property vector3d size: voxelItem.modelSize
        readonly property bool hasModel: size.x > 0 && size.y > 0 && size.z > 0
        readonly property string areaInfo: hasModel ? qsTr("Origin: %1, %2, %3").arg(voxelItem.modelOrigin.x).arg(
                                                          voxelItem.modelOrigin.y).arg(voxelItem.modelOrigin.z) + "\n"
                                                      + qsTr("Size: %1 × %2 × %3").arg(size.x).arg(size.y).arg(size.z) :
                                                      ""

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        visible: !root.controller || !root.controller.loading
        wrapMode: Text.Wrap
        text: (areaInfo.length > 0 ? areaInfo + "\n" : "") + (voxelItem.rotationLocked ? (Theme.touch ? qsTr("Rotation locked · Two fingers: pan and zoom") :
                                                                                                        qsTr("Rotation locked · Right drag: pan · Wheel: zoom")) :
                                                                                         Theme.touch ? qsTr(
                                                                                                           "One finger: rotate · Two fingers: pan and zoom") :
                                                                                                       qsTr("Left drag: rotate · Right drag: pan · Wheel: zoom"))
              + (voxelItem.selectionEnabled ? "\n" + (Theme.touch ? qsTr("Drag a handle to edit the selection") : qsTr(
                                                                        "Middle drag a handle to edit the selection")) :
                                              "")
        color: "#aab3c0"
        font.pixelSize: Theme.fontCaption
    }
}

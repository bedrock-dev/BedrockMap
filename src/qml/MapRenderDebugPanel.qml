import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BedrockMap 1.0
import "controls"

Item {
    id: root

    property var terrainLayer
    property var mapOverlay
    property var terrainData
    property font debugFont: Qt.font({
                                         pointSize: 8
                                     })
    readonly property font panelFont: debugFont
    readonly property real summaryContentWidth: Math.max(timingHeading.implicitWidth, rendererInfo.implicitWidth,
                                                         timingZoom.implicitWidth, atlasState.implicitWidth,
                                                         uploadState.implicitWidth, viewportState.implicitWidth)
    readonly property real preferredPanelWidth: Math.min(560, Math.max(debugActions.implicitWidth + 8,
                                                                       summaryContentWidth + 16))

    function snapshot() {
        return timingHeading.text + "\n" + rendererInfo.text + "\n" + timingZoom.text + "\n" + atlasState.text + "\n" + uploadState.text
                + "\n" + viewportState.text + "\n----------------------\n" + timingTree.text
    }

    AppMenu {
        id: debugCopyMenu
        property string snapshotText: ""

        AppMenuItem {
            text: "Copy"
            onTriggered: root.terrainLayer.copyDebugText(debugCopyMenu.snapshotText)
        }
        AppMenuItem {
            text: "Reset timings"
            onTriggered: root.terrainLayer.resetTimings()
        }
    }

    Rectangle {
        id: renderPanel
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 72
        anchors.rightMargin: 18
        implicitWidth: root.preferredPanelWidth
        implicitHeight: renderColumn.implicitHeight + 16
        width: Math.max(0, Math.min(implicitWidth, parent.width - 36))
        height: Math.max(0, Math.min(implicitHeight, parent.height - 90))
        radius: Theme.radiusSmall
        color: Theme.overlayTranslucent
        border.width: 0
        z: 5

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: function (eventPoint) {
                debugCopyMenu.snapshotText = root.snapshot()
                debugCopyMenu.popup(renderPanel, eventPoint.position.x, eventPoint.position.y)
            }
        }

        Row {
            id: debugActions
            z: 2
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 4
            spacing: 2

            Button {
                text: "Reset"
                flat: true
                font: root.panelFont
                leftPadding: 4
                rightPadding: 4
                topPadding: 0
                bottomPadding: 0
                implicitHeight: 16
                onClicked: root.terrainLayer.resetTimings()
            }
            Button {
                text: "Copy"
                flat: true
                font: root.panelFont
                leftPadding: 4
                rightPadding: 4
                topPadding: 0
                bottomPadding: 0
                implicitHeight: 16
                onClicked: root.terrainLayer.copyDebugText(root.snapshot())
            }
        }

        Flickable {
            anchors.fill: parent
            anchors.margins: 8
            anchors.topMargin: 24
            contentWidth: Math.max(width, timingTree.implicitWidth)
            contentHeight: renderColumn.implicitHeight
            clip: true

            Column {
                id: renderColumn
                width: Math.max(renderPanel.width - 16, timingTree.implicitWidth)
                spacing: 1

                Text {
                    id: timingHeading
                    width: Math.max(0, renderPanel.width - 16)
                    text: "RENDERER DEBUG  |  %1 FPS".arg(root.terrainLayer.framesPerSecond.toFixed(1))
                    color: Theme.overlayText
                    font.pointSize: root.panelFont.pointSize
                    wrapMode: Text.Wrap
                }

                Text {
                    id: rendererInfo
                    width: Math.max(0, renderPanel.width - 16)
                    color: Theme.overlayText
                    font: root.panelFont
                    text: root.terrainLayer.graphicsInfo
                    wrapMode: Text.Wrap
                }
                Text {
                    id: timingZoom
                    width: Math.max(0, renderPanel.width - 16)
                    color: Theme.overlayText
                    font: root.panelFont
                    wrapMode: Text.Wrap
                    text: "Zoom %1 px/chunk | %2 px/block | LOD %3 blocks/texel".arg(root.mapOverlay
                                                                                     ? root.mapOverlay.viewScale.toFixed(
                                                                                           2) : "-").arg(
                              root.mapOverlay ? (Math.abs(root.mapOverlay.viewScale) / 16).toFixed(2) : "-").arg(
                              root.terrainLayer.blocksPerTexel)

                }
                Text {
                    id: atlasState
                    width: Math.max(0, renderPanel.width - 16)
                    color: Theme.overlayText
                    font: root.panelFont
                    wrapMode: Text.Wrap
                    text: "Atlas: %1×%2 | terrain %3 | loading %4 | empty %5".arg(
                              root.terrainLayer.atlasSlotsPerSide).arg(root.terrainLayer.atlasSlotsPerSide).arg(
                              root.terrainLayer.terrainSlotCount).arg(root.terrainLayer.loadingSlotCount).arg(
                              root.terrainLayer.emptySlotCount)
                }
                Text {
                    id: uploadState
                    width: Math.max(0, renderPanel.width - 16)
                    color: Theme.overlayText
                    font: root.panelFont
                    text: "Uploads: %1 terrain tiles".arg(root.terrainLayer.uploadedTiles)
                }
                Text {
                    id: viewportState
                    width: Math.max(0, renderPanel.width - 16)
                    color: Theme.overlayText
                    font: root.panelFont
                    text: "Viewport: %1 × %2 px".arg(Math.round(root.width)).arg(Math.round(root.height))
                }
                Rectangle {
                    width: Math.max(0, renderPanel.width - 16)
                    height: 1
                    color: Theme.overlayBorder
                }
                Text {
                    id: timingTree
                    text: root.terrainLayer.timingText
                    textFormat: Text.PlainText
                    color: Theme.overlayText
                    font: root.panelFont
                }
            }
        }
    }

    Rectangle {
        id: loaderPanel
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: parent.width < renderPanel.width + implicitWidth + 36 ? renderPanel.y + renderPanel.height
                                                                                   + 10 : 72
        anchors.leftMargin: 18
        implicitWidth: loaderColumn.implicitWidth + 16
        implicitHeight: loaderColumn.implicitHeight + 16
        width: implicitWidth
        height: implicitHeight
        radius: Theme.radiusSmall
        color: Theme.overlayTranslucent
        border.width: 0
        z: 5

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: function (eventPoint) {
                const lines = root.terrainData ? root.terrainData.loaderDebug : []
                debugCopyMenu.snapshotText = lines.join("\n") + "\n" + memoryUsage.text
                debugCopyMenu.popup(loaderPanel, eventPoint.position.x, eventPoint.position.y)
            }
        }

        Column {
            id: loaderColumn
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 8
            spacing: 0

            Repeater {
                model: root.terrainData ? root.terrainData.loaderDebug : []
                delegate: Text {
                    required property string modelData
                    text: modelData
                    color: Theme.overlayText
                    font: root.panelFont
                }
            }
            Text {
                id: memoryUsage
                text: qsTr("Memory usage: %1 MiB").arg(root.terrainData ? root.terrainData.memoryUsageMiB.toFixed(1) :
                                                                          "-")

                color: Theme.overlayText
                font: root.panelFont
            }
        }
    }
}

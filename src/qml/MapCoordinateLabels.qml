import BedrockMap 1.0
import QtQuick

Item {
    id: root
    property double centerX: 0
    property double centerZ: 0
    property double pixelsPerChunk: 64
    property bool labelsVisible: false
    property var labels: []
    // Below one block per two pixels the overview is too dense for fixed-size
    // coordinate labels. This is a presentation rule, not a user setting.
    readonly property real minimumScale: 8

    clip: true

    function rebuild() {
        if (!labelsVisible) {
            if (labels.length !== 0)
                labels = []
            return
        }
        if (pixelsPerChunk < minimumScale || width <= 0 || height <= 0) {
            if (labels.length !== 0)
                labels = []
            return
        }
        const step = pixelsPerChunk >= 128 ? 1 : 32
        const halfWidth = width / (2 * pixelsPerChunk)
        const halfHeight = height / (2 * pixelsPerChunk)
        const minX = Math.floor(centerX - halfWidth) - 1
        const maxX = Math.ceil(centerX + halfWidth) + 1
        const minZ = Math.floor(centerZ - halfHeight) - 1
        const maxZ = Math.ceil(centerZ + halfHeight) + 1
        const startX = Math.ceil(minX / step) * step
        const startZ = Math.ceil(minZ / step) * step
        const result = []
        for (let z = startZ; z <= maxZ; z += step) {
            const py = height / 2 + (z - centerZ) * pixelsPerChunk + 2
            for (let x = startX; x <= maxX; x += step) {
                const px = width / 2 + (x - centerX) * pixelsPerChunk + 2
                result.push({
                                px: px,
                                py: py,
                                text: (x * 16) + "," + (z * 16)
                            })
            }
        }
        labels = result
    }

    onWidthChanged: rebuild()
    onHeightChanged: rebuild()
    onCenterXChanged: rebuild()
    onCenterZChanged: rebuild()
    onPixelsPerChunkChanged: rebuild()
    onLabelsVisibleChanged: rebuild()
    Component.onCompleted: rebuild()

    Repeater {
        model: root.labels
        delegate: Rectangle {
            required property var modelData
            x: modelData.px
            y: modelData.py
            width: labelText.implicitWidth + 4
            height: labelText.implicitHeight + 2
            color: "#5a161616"
            radius: 1
            Text {
                id: labelText
                anchors.centerIn: parent
                text: modelData.text
                color: "#ffffff"
                font.pointSize: 8
            }
        }
    }
}

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

Item {
    id: root
    property var model
    property int selectedRow: -1
    signal editRequested(int row)
    signal contextRequested(int row, real x, real y)

    function selectRow(row) {
        if (row < 0 || !model || row >= model.rowCount())
            return
        selectedRow = row
    }

    readonly property var tagLetters: ["E", "B", "S", "I", "L", "F", "D", "B", "S", "L", "C", "I", "L"]
    readonly property var tagColors: ["#64748b", "#2563eb", "#7c3aed", "#0e7490", "#0f766e", "#b45309", "#c2410c",
                                      "#1d4ed8", "#15803d", "#be185d", "#7e22ce", "#155e75", "#115e59"]

    function tagLetter(type) {
        return type >= 0 && type < tagLetters.length ? tagLetters[type] : "?"
    }
    function tagColor(type) {
        return type >= 0 && type < tagColors.length ? tagColors[type] : "#475569"
    }

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        model: root.model
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        delegate: Rectangle {
            id: row
            required property int index
            required property string key
            required property int tagType
            required property string typeName
            required property string value
            required property string icon
            required property int depth
            required property bool hasChildren
            required property bool expanded

            width: list.width
            height: Theme.touch ? Theme.touchTarget : 28
            radius: Theme.radiusSmall
            color: index === root.selectedRow ? Theme.accentSoft : "transparent"
            border.width: index === root.selectedRow ? 1 : 0
            border.color: Theme.accent

            readonly property real arrowX: 8 + row.depth * 16
            readonly property real iconX: row.arrowX + 20
            readonly property real keyX: row.iconX + 22
            readonly property real keyWidth: Math.max(90, list.width * 0.28)
            readonly property real valueX: row.keyX + row.keyWidth + 8

            function requestContext(sceneX, sceneY) {
                root.selectRow(row.index)
                root.contextRequested(row.index, sceneX, sceneY)
            }

            Text {
                x: row.arrowX
                y: (row.height - height) / 2
                width: 16
                height: 18
                visible: row.hasChildren
                text: row.expanded ? "▾" : "▸"
                color: Theme.muted
                font.pixelSize: Theme.fontBody
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            MouseArea {
                id: arrowMouse
                x: row.arrowX
                y: 0
                width: 16
                height: row.height
                enabled: row.hasChildren
                z: 3
                onClicked: if (root.model)
                               root.model.toggleExpanded(row.index)
                onPressAndHold: function (mouse) {
                    const point = row.mapToItem(null, mouse.x + row.arrowX, mouse.y)
                    row.requestContext(point.x, point.y)
                }
            }
            Rectangle {
                x: row.iconX
                y: (row.height - height) / 2
                width: 16
                height: 16
                radius: 4
                color: root.tagColor(row.tagType)
                Text {
                    anchors.centerIn: parent
                    text: root.tagLetter(row.tagType)
                    color: "#ffffff"
                    font.pixelSize: 9
                    font.bold: true
                }
            }
            Text {
                id: keyLabel
                x: row.keyX
                y: (row.height - height) / 2
                width: row.keyWidth
                height: 18
                text: row.key
                color: Theme.text
                font.family: "JetBrains Mono"
                font.pixelSize: Theme.fontBody
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
                maximumLineCount: 1
                verticalAlignment: Text.AlignVCenter
            }
            MouseArea {
                id: keyMouse
                x: row.keyX
                y: 0
                width: row.keyWidth
                height: row.height
                acceptedButtons: Qt.LeftButton
                z: 3
                onClicked: root.selectRow(row.index)
                onDoubleClicked: {
                    root.selectRow(row.index)
                    root.editRequested(row.index)
                }
                onPressAndHold: function (mouse) {
                    const point = row.mapToItem(null, mouse.x + row.keyX, mouse.y)
                    row.requestContext(point.x, point.y)
                }
            }
            Text {
                x: row.valueX
                y: (row.height - height) / 2
                width: Math.max(0, list.width - row.valueX - 8)
                height: 18
                text: row.value
                color: Theme.muted
                font.family: "JetBrains Mono"
                font.pixelSize: Theme.fontBody
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
                maximumLineCount: 1
                verticalAlignment: Text.AlignVCenter
            }

            MouseArea {
                id: rowMouse
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function (mouse) {
                    root.selectRow(row.index)
                    if (mouse.button === Qt.RightButton) {
                        const scene = row.mapToItem(null, mouse.x, mouse.y)
                        root.contextRequested(row.index, scene.x, scene.y)
                    }
                }
                onDoubleClicked: {
                    root.selectRow(row.index)
                    if (row.hasChildren)
                        root.model.toggleExpanded(row.index)
                    else
                        root.editRequested(row.index)
                }
                onPressAndHold: function (mouse) {
                    const point = rowMouse.mapToItem(null, mouse.x, mouse.y)
                    row.requestContext(point.x, point.y)
                }
            }
        }
    }
}

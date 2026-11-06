import BedrockMap 1.0
import QtQuick

Item {
    id: root

    property int tagType: 0
    property real iconSize: Theme.iconSmallSize
    readonly property var letters: ["E", "B", "S", "I", "L", "F", "D", "B", "S", "L", "C", "I", "L"]
    readonly property var colors: ["#64748b", "#2563eb", "#7c3aed", "#0e7490", "#0f766e", "#b45309",
        "#c2410c", "#1d4ed8", "#15803d", "#be185d", "#7e22ce", "#155e75", "#115e59"]

    implicitWidth: iconSize
    implicitHeight: iconSize

    Rectangle {
        anchors.fill: parent
        radius: Theme.ui(4)
        color: root.tagType >= 0 && root.tagType < root.colors.length ? root.colors[root.tagType] : "#475569"

        Text {
            anchors.centerIn: parent
            text: root.tagType >= 0 && root.tagType < root.letters.length ? root.letters[root.tagType] : "?"
            color: "#ffffff"
            font.pixelSize: Theme.ui(9)
            font.bold: true
        }
    }
}

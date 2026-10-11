import QtQuick

Item {
    id: page
    property var colors
    Flickable { anchors.fill: parent; contentHeight: column.height + 40; clip: true; Column { id: column; x: 28; y: 12; width: parent.width - 56; spacing: 12
        Text { text: qsTr("Tools"); color: colors.text; font.pixelSize: 17; font.bold: true }
        Text { text: qsTr("Useful actions for inspecting and editing a world."); color: colors.muted; font.pixelSize: 12 }
        ToolRow { title: qsTr("NBT editor"); detail: qsTr("Inspect level and chunk data"); icon: "{}"; colors: page.colors }
        ToolRow { title: qsTr("Coordinate tools"); detail: qsTr("Find positions and dimensions"); icon: "⌖"; colors: page.colors }
        ToolRow { title: qsTr("Export and snapshots"); detail: qsTr("Create a portable map image"); icon: "↗"; colors: page.colors }
    } }
    component ToolRow: Rectangle { property var colors; property string title; property string detail; property string icon; width: parent.width; height: 70; radius: 11; color: colors.surface; border.color: colors.border; Row { anchors.fill: parent; anchors.margins: 14; spacing: 14; Rectangle { width: 42; height: 42; radius: 10; color: colors.accentSoft; Text { anchors.centerIn: parent; text: icon; color: colors.accent; font.pixelSize: 18 } } Column { anchors.verticalCenter: parent.verticalCenter; spacing: 4; Text { text: title; color: colors.text; font.bold: true; font.pixelSize: 13 } Text { text: detail; color: colors.muted; font.pixelSize: 11 } } } }
}

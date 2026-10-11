import QtQuick

Item {
    id: page
    signal openWorld(string path, string title)
    property var colors
    property var recentWorlds: worldModel ? worldModel.recentWorlds : []
    property var allWorlds: worldModel ? worldModel.allWorlds : []
    Flickable { anchors.fill: parent; contentHeight: body.height + 40; clip: true; Column { id: body; x: 28; y: 10; width: parent.width - 56; spacing: 22
        Row { width: parent.width; spacing: 10; Text { text: qsTr("Your worlds"); color: colors.text; font.pixelSize: 17; font.bold: true } Item { width: 1; height: 1 } Rectangle { width: 112; height: 36; radius: 9; color: colors.accent; Text { anchors.centerIn: parent; text: qsTr("＋ Add world"); color: "#102116"; font.bold: true; font.pixelSize: 12 } MouseArea { anchors.fill: parent } } }
        Text { text: qsTr("A calm place to start your next journey."); color: colors.muted; font.pixelSize: 12 }
        Rectangle { width: parent.width; height: 1; color: colors.border }
        Text { text: qsTr("Recent worlds"); color: colors.text; font.pixelSize: 14; font.bold: true }
        ListView { width: parent.width; height: Math.max(0, contentHeight); interactive: false; spacing: 10; model: page.recentWorlds; delegate: WorldCard { width: ListView.view.width; colors: page.colors; title: String((modelData && modelData.name) || ""); version: String((modelData && modelData.version) || ""); tag: String((modelData && modelData.time) || ""); sizeText: String((modelData && modelData.size) || ""); sourceText: String((modelData && modelData.source) || ""); pathText: String((modelData && modelData.path) || ""); onOpenRequested: page.openWorld(path, title) } }
        Text { text: qsTr("All worlds"); color: colors.text; font.pixelSize: 14; font.bold: true }
        ListView { width: parent.width; height: Math.max(0, contentHeight); interactive: false; spacing: 10; model: page.allWorlds; delegate: WorldCard { width: ListView.view.width; colors: page.colors; title: String((modelData && modelData.name) || ""); version: String((modelData && modelData.version) || ""); tag: String((modelData && modelData.time) || ""); sizeText: String((modelData && modelData.size) || ""); sourceText: String((modelData && modelData.source) || ""); pathText: String((modelData && modelData.path) || ""); onOpenRequested: page.openWorld(path, title) } }
        Rectangle { visible: page.allWorlds.length === 0; width: parent.width; height: 62; radius: 10; color: colors.surface; border.color: colors.border; Row { anchors.fill: parent; anchors.margins: 10; spacing: 12; Rectangle { width: 74; height: 42; radius: 6; color: colors.accentSoft; Text { anchors.centerIn: parent; text: "◆"; color: colors.accent; font.pixelSize: 22 } } Column { anchors.verticalCenter: parent.verticalCenter; spacing: 4; Text { text: qsTr("No imported worlds yet"); color: colors.text; font.bold: true; font.pixelSize: 13 } Text { text: qsTr("Open a Bedrock world to see it here"); color: colors.muted; font.pixelSize: 11 } } } }
    } }
}

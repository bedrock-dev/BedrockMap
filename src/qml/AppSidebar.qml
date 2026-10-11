import QtQuick

Rectangle {
    id: root
    property var colors
    property int currentIndex: 0
    property var sessions
    property int currentMapIndex: -1
    property bool mapsExpanded: true
    property bool darkMode: true
    signal navigate(int index)
    signal mapSelected(int index)
    signal toggleTheme()
    color: colors.surface; border.color: colors.border; border.width: 1
    Column {
        anchors.fill: parent; anchors.margins: 18; spacing: 8
        Row { spacing: 10; height: 44; Rectangle { width: 34; height: 34; radius: 10; color: colors.accent; Image { anchors.centerIn: parent; width: 24; height: 24; source: "qrc:/BedrockMap.svg"; smooth: true } } Column { Text { text: "BedrockMap"; color: colors.text; font.bold: true; font.pixelSize: 16 } Text { text: "World workspace"; color: colors.muted; font.pixelSize: 10 } } }
        Item { width: 1; height: 18 }
        SidebarItem { label: qsTr("Worlds"); icon: "▦"; selected: currentIndex === 0; colors: parent.parent.colors; onClicked: navigate(0) }
        SidebarItem { label: qsTr("Maps"); icon: "⌖"; selected: false; colors: parent.parent.colors; onClicked: mapsExpanded = !mapsExpanded }
        Repeater { model: mapsExpanded ? (root.sessions || []) : []; delegate: Rectangle { required property string title; required property int sessionIndex; width: parent.width; height: 38; radius: 8; color: sessionIndex === root.currentMapIndex ? colors.accentSoft : "transparent"
                Row { anchors.fill: parent; anchors.leftMargin: 38; spacing: 8; Text { anchors.verticalCenter: parent.verticalCenter; text: "•"; color: sessionIndex === root.currentMapIndex ? colors.accent : colors.muted; font.pixelSize: 14 } Text { anchors.verticalCenter: parent.verticalCenter; text: title || qsTr("Untitled world"); color: sessionIndex === root.currentMapIndex ? colors.accent : colors.text; font.pixelSize: 12; elide: Text.ElideRight; width: parent.width - 52 } }
                MouseArea { anchors.fill: parent; onClicked: root.mapSelected(sessionIndex) }
            } }
        SidebarItem { label: qsTr("Global data"); icon: "▤"; selected: currentIndex === 2; colors: parent.parent.colors; onClicked: navigate(2) }
        SidebarItem { label: qsTr("Tools"); icon: "◇"; selected: currentIndex === 3; colors: parent.parent.colors; onClicked: navigate(3) }
        Item { width: 1; height: 1 }
        SidebarItem { label: qsTr("Settings"); icon: "⚙"; selected: currentIndex === 4; colors: parent.parent.colors; onClicked: navigate(4) }
        Rectangle { width: parent.width; height: 1; color: colors.border }
        Text { text: qsTr("PREVIEW"); color: colors.muted; font.pixelSize: 10; font.bold: true }
        Text { text: qsTr("QML interface"); color: colors.accent; font.pixelSize: 12 }
        Item { width: 1; height: 1 }
        Text { text: qsTr("Ready for touch"); color: colors.muted; font.pixelSize: 11 }
        Rectangle { width: parent.width; height: 40; radius: 9; color: themeMouse.containsMouse ? colors.surfaceAlt : "transparent"
            Row { anchors.fill: parent; anchors.leftMargin: 12; spacing: 10; Text { anchors.verticalCenter: parent.verticalCenter; text: darkMode ? "☾" : "☀"; color: colors.muted; font.pixelSize: 17 } Text { anchors.verticalCenter: parent.verticalCenter; text: darkMode ? qsTr("Dark mode") : qsTr("Light mode"); color: colors.text; font.pixelSize: 12 } }
            MouseArea { id: themeMouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.toggleTheme() }
        }
    }
    component SidebarItem: Rectangle {
        property string label; property string icon; property bool selected; property var colors
        signal clicked()
        width: parent.width; height: 46; radius: 10; color: selected ? colors.accentSoft : (hover.containsMouse ? colors.surfaceAlt : "transparent")
        Row { anchors.fill: parent; anchors.leftMargin: 13; spacing: 13; Text { width: 20; anchors.verticalCenter: parent.verticalCenter; text: icon; color: selected ? colors.accent : colors.muted; font.pixelSize: 20; horizontalAlignment: Text.AlignHCenter } Text { anchors.verticalCenter: parent.verticalCenter; text: label; color: selected ? colors.accent : colors.text; font.pixelSize: 13; font.bold: selected } }
        MouseArea { id: hover; anchors.fill: parent; hoverEnabled: true; onClicked: parent.clicked() }
    }
}

import QtQuick

Item {
    id: page
    property var colors; property bool darkMode: true; signal toggleTheme()
    Column { x: 28; y: 12; width: Math.min(parent.width - 56, 620); spacing: 18
        Text { text: qsTr("Settings"); color: colors.text; font.pixelSize: 17; font.bold: true }
        Text { text: qsTr("Tune the workspace for your screen and workflow."); color: colors.muted; font.pixelSize: 12 }
        SettingRow { title: qsTr("Appearance"); detail: darkMode ? qsTr("Dark theme") : qsTr("Light theme"); colors: page.colors; actionText: darkMode ? "☾" : "☀"; onClicked: toggleTheme() }
        SettingRow { title: qsTr("Renderer"); detail: qsTr("Automatic"); colors: page.colors; actionText: "›" }
        SettingRow { title: qsTr("Touch mode"); detail: qsTr("Larger controls on small screens"); colors: page.colors; actionText: "›" }
    }
    component SettingRow: Rectangle { property var colors; property string title; property string detail; property string actionText; signal clicked(); width: parent.width; height: 64; radius: 11; color: colors.surface; border.color: colors.border; Row { anchors.fill: parent; anchors.margins: 15; Column { anchors.verticalCenter: parent.verticalCenter; spacing: 4; Text { text: title; color: colors.text; font.bold: true; font.pixelSize: 13 } Text { text: detail; color: colors.muted; font.pixelSize: 11 } } Item { width: 1; height: 1 } Text { anchors.verticalCenter: parent.verticalCenter; text: actionText; color: colors.accent; font.pixelSize: 19 } } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
}

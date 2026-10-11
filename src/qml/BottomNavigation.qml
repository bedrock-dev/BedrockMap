import QtQuick

Rectangle {
    property var colors; property int currentIndex: 0; signal navigate(int index)
    color: colors.surface; border.color: colors.border; border.width: 1
    Row { anchors.fill: parent; anchors.margins: 8; Repeater { model: [{label: qsTr("Worlds"), icon: "▦"}, {label: qsTr("Maps"), icon: "⌖"}, {label: qsTr("Global"), icon: "▤"}, {label: qsTr("Tools"), icon: "◇"}, {label: qsTr("Settings"), icon: "⚙"}]; delegate: Rectangle { required property var modelData; width: parent.width / 5; height: parent.height; radius: 10; color: index === currentIndex ? colors.accentSoft : "transparent"; Column { anchors.centerIn: parent; spacing: 2; Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.icon; color: index === currentIndex ? colors.accent : colors.muted; font.pixelSize: 18 } Text { text: modelData.label; color: index === currentIndex ? colors.accent : colors.muted; font.pixelSize: 9 } } MouseArea { anchors.fill: parent; onClicked: navigate(index) } } } }
}

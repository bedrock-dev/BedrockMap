import QtQuick

Rectangle {
    signal openRequested(string path, string title)
    property var colors; property string title; property string version; property string tag
    property string sizeText; property string sourceText; property string pathText
    height: 92; radius: 12; color: colors.surface; border.color: hover.containsMouse ? colors.accent : colors.border; border.width: hover.containsMouse ? 2 : 1
    Row { anchors.fill: parent; anchors.margins: 12; spacing: 12
        Rectangle { width: 76; height: parent.height; radius: 8; color: colors.map; Text { anchors.centerIn: parent; text: "✦"; color: colors.accent; opacity: 0.75; font.pixelSize: 28 } }
        Column { anchors.verticalCenter: parent.verticalCenter; width: parent.width - 88; spacing: 7
            Text { text: title; color: colors.text; font.bold: true; font.pixelSize: 14; elide: Text.ElideRight; width: parent.width }
            Row { spacing: 8; Text { text: version || qsTr("Unknown version"); color: colors.accent; font.pixelSize: 11 } Text { text: "•"; color: colors.muted } Text { text: tag || qsTr("Unknown time"); color: colors.muted; font.pixelSize: 11 } Text { text: "•"; color: colors.muted } Text { text: sizeText || "0 B"; color: colors.muted; font.pixelSize: 11 } }
            Text { text: (sourceText || qsTr("Bedrock")) + "  ·  " + pathText; color: colors.muted; font.pixelSize: 10; elide: Text.ElideMiddle; width: parent.width }
        }
    }
    MouseArea { id: hover; anchors.fill: parent; hoverEnabled: true; onClicked: openRequested(pathText, title) }
}

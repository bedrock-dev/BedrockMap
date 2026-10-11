import QtQuick

Item {
    id: page
    property var colors
    property var sessions
    signal mapSelected(int index)

    Flickable { anchors.fill: parent; contentHeight: body.height + 40; clip: true
        Column { id: body; x: 28; y: 12; width: parent.width - 56; spacing: 12
            Text { text: qsTr("Open maps"); color: colors.text; font.pixelSize: 18; font.bold: true }
            Text { text: qsTr("Select an opened world map to continue."); color: colors.muted; font.pixelSize: 12 }
            Repeater { model: page.sessions || []; delegate: Rectangle { required property string title; width: body.width; height: 64; radius: 10; color: colors.surface; border.color: colors.border
                    Row { anchors.fill: parent; anchors.margins: 12; spacing: 12; Text { anchors.verticalCenter: parent.verticalCenter; text: "⌖"; color: colors.accent; font.pixelSize: 22 } Text { anchors.verticalCenter: parent.verticalCenter; text: title || qsTr("Untitled world"); color: colors.text; font.pixelSize: 14; elide: Text.ElideRight; width: parent.width - 54 } }
                    MouseArea { anchors.fill: parent; onClicked: page.mapSelected(index) }
                } }
            Text { visible: !page.sessions || page.sessions.count === 0; text: qsTr("No maps are open yet."); color: colors.muted; font.pixelSize: 13 }
        }
    }
}

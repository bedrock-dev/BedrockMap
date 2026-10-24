import BedrockMap 1.0
import QtQuick
import "controls"

Item {
    id: page
    property var sessions
    signal mapSelected(int index)

    Flickable {
        anchors.fill: parent
        contentHeight: body.height + 40
        clip: true
        Column {
            id: body
            x: 28
            y: 12
            width: parent.width - 56
            spacing: Theme.gapLarge
            Text {
                text: qsTr("Open maps")
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.bold: true
            }
            Text {
                text: qsTr("Select an opened world map to continue.")
                color: Theme.muted
                font.pixelSize: Theme.fontSubtitle
            }
            Repeater {
                model: page.sessions || []
                delegate: Rectangle {
                    required property string title
                    width: body.width
                    height: 64
                    radius: Theme.radiusCard
                    color: Theme.surface
                    border.color: Theme.border
                    Row {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: Theme.gapLarge
                        AppIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 20
                            height: 20
                            name: "locate"
                            iconSize: width
                            iconColor: Theme.accent
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: title || qsTr("Untitled world")
                            color: Theme.text
                            font.pixelSize: Theme.fontLabel
                            elide: Text.ElideRight
                            width: parent.width - 54
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: page.mapSelected(index)
                    }
                }
            }
            Text {
                visible: !page.sessions || page.sessions.count === 0
                text: qsTr("No maps are open yet.")
                color: Theme.muted
                font.pixelSize: Theme.fontSubtitle
            }
        }
    }
}

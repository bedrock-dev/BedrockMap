import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import "controls"

Item {
    id: page

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: body.y + body.height + Theme.gapSection
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        clip: true
        ScrollBar.vertical: AppScrollBar {}

        Column {
            id: body
            x: Theme.gapSection
            y: Theme.gapMedium
            width: parent.width - Theme.gapSection * 2
            spacing: Theme.gapMedium

            Text {
                width: parent.width
                text: App.openSessions.count === 1 ? qsTr("1 session open") : qsTr("%1 sessions open").arg(
                                                         App.openSessions.count)
                color: Theme.muted
                font.pixelSize: Theme.fontMedium
            }

            Repeater {
                model: App.openSessions
                delegate: SessionListRow {
                    width: body.width
                    preferredHeight: Theme.sessionRowHeight
                    pageMode: true
                    showPath: true
                    active: App.workspaceOpen && sessionIndex === App.workspaceIndex
                    onCloseRequested: App.closeSession(sessionIndex)
                }
            }

            Rectangle {
                visible: App.openSessions.count === 0
                width: parent.width
                height: 96
                radius: Theme.radiusLarge
                color: Theme.surface
                border.color: Theme.border
                Column {
                    anchors.centerIn: parent
                    spacing: Theme.gapSmall
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("No open sessions")
                        color: Theme.text
                        font.pixelSize: Theme.fontMedium
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Open a world or NBT document from the Welcome page.")
                        color: Theme.muted
                        font.pixelSize: Theme.fontMedium
                    }
                }
            }
        }
    }
}

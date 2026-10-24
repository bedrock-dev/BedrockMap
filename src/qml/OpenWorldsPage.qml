import BedrockMap 1.0
import QtQuick
import "controls"

Item {
    id: page

    Flickable {
        anchors.fill: parent
        contentHeight: body.height + Theme.gapSection
        clip: true

        Column {
            id: body
            x: Theme.pageGutter
            y: Theme.gapMedium
            width: parent.width - Theme.pageGutter * 2
            spacing: Theme.gapMedium

            Text {
                width: parent.width
                text: App.openSessions.count === 1 ? qsTr("1 session open") : qsTr("%1 sessions open").arg(
                                                                   App.openSessions.count)
                color: Theme.muted
                font.pixelSize: Theme.fontBody
            }

            Repeater {
                model: App.openSessions
                delegate: Rectangle {
                    required property int sessionIndex
                    required property string title
                    required property string path
                    required property bool dirty
                    required property string kind
                    readonly property bool active: App.workspaceOpen && sessionIndex
                                                   === App.workspaceIndex

                    width: body.width
                    height: Theme.compact ? 52 : 64
                    radius: Theme.radiusCard
                    color: active ? Theme.accentSoft : ((rowMouse.pressed || rowMouse.containsMouse) ? Theme.surfaceAlt : (
                                                                                            Theme.compact ? "transparent" :
                                                                                                            Theme.surface))
                    border.width: active ? 1 : (Theme.compact ? 0 : 1)
                    border.color: active ? Theme.accent : Theme.border

                    Row {
                        anchors.fill: parent
                        anchors.margins: Theme.cardPadding
                        spacing: Theme.gapMedium

                        Rectangle {
                            width: 34
                            height: 34
                            radius: Theme.radiusControl
                            color: active ? Theme.accent : Theme.accentSoft
                            anchors.verticalCenter: parent.verticalCenter
                            AppIcon {
                                anchors.centerIn: parent
                                width: 18
                                height: 18
                                name: kind === "mcstructure" ? "structure" : kind === "nbt" ? "nbt_editor" : "opened_world_list"
                                iconSize: width
                                iconColor: active ? Theme.onAccent : Theme.accent
                            }
                        }

                        Column {
                            width: parent.width - 84
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                width: parent.width
                                text: (title || qsTr("Untitled world")) + (dirty ? " *" : "")
                                color: active ? Theme.accent : Theme.text
                                font.pixelSize: Theme.fontSubtitle
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: path
                                color: Theme.muted
                                font.pixelSize: Theme.fontCaption
                                elide: Text.ElideMiddle
                            }
                        }
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        anchors.rightMargin: 34
                        hoverEnabled: true
                        onClicked: App.selectWorkspace(sessionIndex)
                    }

                    AppIcon {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 16
                        height: 16
                        name: "close"
                        iconSize: width
                        iconColor: (closeMouse.pressed || closeMouse.containsMouse) ? Theme.danger : Theme.muted
                        MouseArea {
                            id: closeMouse
                            anchors.fill: parent
                            anchors.margins: -6
                            hoverEnabled: true
                            onClicked: App.closeSession(sessionIndex)
                        }
                    }
                }
            }

            Rectangle {
                visible: App.openSessions.count === 0
                width: parent.width
                height: 96
                radius: Theme.radiusCard
                color: Theme.surface
                border.color: Theme.border
                Column {
                    anchors.centerIn: parent
                    spacing: Theme.gapSmall
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("No open sessions")
                        color: Theme.text
                        font.pixelSize: Theme.fontSubtitle
                        font.bold: true
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Open a world or NBT document from the Welcome page.")
                        color: Theme.muted
                        font.pixelSize: Theme.fontBody
                    }
                }
            }
        }
    }
}

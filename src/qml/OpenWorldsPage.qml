import BedrockMap 1.0
import QtQuick
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
                    readonly property bool active: App.workspaceOpen && sessionIndex === App.workspaceIndex

                    width: body.width
                    height: Theme.sessionRowHeight
                    radius: Theme.radiusCard
                    color: active ? Theme.accentSoft : ((rowMouse.pressed || rowMouse.containsMouse) ? Theme.surfaceAlt :
                                                                                                       (Theme.compact
                                                                                                        ? "transparent" :
                                                                                                          Theme.surface))
                    border.width: active ? Theme.borderWidth : (Theme.compact ? 0 : Theme.borderWidth)
                    border.color: active ? Theme.accent : Theme.border

                    Row {
                        anchors.fill: parent
                        anchors.margins: Theme.cardPadding
                        spacing: Theme.gapMedium

                        Rectangle {
                            width: Theme.ui(34)
                            height: width
                            radius: Theme.radiusControl
                            color: active ? Theme.accent : Theme.accentSoft
                            anchors.verticalCenter: parent.verticalCenter
                            AppIcon {
                                anchors.centerIn: parent
                                width: Theme.iconSize
                                height: width
                                name: kind === "mcstructure" ? "structure" : kind === "nbt" ? "nbt_editor" :
                                                                                              "opened_world_list"
                                iconSize: width
                                iconColor: active ? Theme.onAccent : Theme.accent
                            }
                        }

                        Column {
                            width: Math.max(0, parent.width - Theme.ui(34) - parent.spacing - closeButton.controlSize
                                            - Theme.gapSmall)
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
                        anchors.rightMargin: closeButton.width + closeButton.anchors.rightMargin
                        hoverEnabled: true
                        onClicked: App.selectWorkspace(sessionIndex)
                    }

                    AppIconButton {
                        id: closeButton
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.gapSmall
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "close"
                        label: qsTr("Close session")
                        buttonSize: 28
                        danger: true
                        onClicked: App.closeSession(sessionIndex)
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

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import "controls"

AppCard {
    id: card
    interactive: true
    hovered: hover.containsMouse || hover.pressed || actionsButton.hovered || cardMenu.visible
    signal openRequested(string path, string title)
    signal deleteRequested(string path, string title)
    signal exportRequested(string path, string title)
    signal exportAndOpenRequested(string path, string title)
    property string title
    property string versionText
    property string tag
    property string sizeText
    property string sourceText
    property string pathText
    property url thumbnailSource
    property bool xorMarked
    property bool gridMode: false
    property bool appInternal: false

    component WorldThumbnail: Rectangle {
        color: Theme.surfaceAlt
        radius: Theme.radiusControl
        clip: true
        AppIcon {
            anchors.centerIn: parent
            width: 28
            height: 28
            name: "broken_image"
            iconSize: width
            iconColor: Theme.accent
            opacity: 0.75
        }
        Image {
            anchors.fill: parent
            source: card.thumbnailSource
            visible: status === Image.Ready
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            smooth: true
        }
    }

    Row {
        visible: !card.gridMode
        anchors.fill: parent
        anchors.margins: Theme.cardPadding
        spacing: Theme.gapLarge

        WorldThumbnail {
            id: listThumbnail
            width: Theme.compact ? 64 : 76
            height: parent.height
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, parent.width - listThumbnail.width - actionsButton.width - Theme.gapLarge * 2)
            spacing: Theme.gapSmall

            Row {
                width: parent.width
                spacing: Theme.gapSmall
                Text {
                    text: card.title
                    color: Theme.text
                    font.bold: true
                    font.pixelSize: Theme.fontLabel
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, Math.max(0, parent.width - (card.xorMarked ? listBadge.implicitWidth
                                                                                                + parent.spacing : 0)))
                }
                Text {
                    id: listBadge
                    visible: card.xorMarked
                    text: "[XOR]"
                    color: Theme.danger
                    font.bold: true
                    font.pixelSize: Theme.fontBody
                }
            }
            Row {
                width: parent.width
                spacing: Theme.gapMedium
                Text {
                    id: listVersionText
                    text: card.versionText || qsTr("Unknown version")
                    color: Theme.accent
                    font.pixelSize: Theme.fontBody
                    width: Math.min(implicitWidth, Math.max(0, parent.width * 0.55))
                    elide: Text.ElideRight
                }
                Text {
                    text: "•"
                    color: Theme.muted
                }
                Text {
                    width: Math.max(0, parent.width - listVersionText.width - parent.spacing * 2 - 8)
                    text: (card.tag || qsTr("Unknown time")) + "  ·  " + (card.sizeText || "0 B")
                    color: Theme.muted
                    font.pixelSize: Theme.fontBody
                    elide: Text.ElideRight
                }
            }
            Text {
                text: (card.sourceText ? qsTranslate("WorldSource", card.sourceText) : qsTr("Bedrock")) + "  ·  "
                      + card.pathText
                color: Theme.muted
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideMiddle
                width: parent.width
            }
        }
    }

    Column {
        id: gridContent
        visible: card.gridMode
        anchors.fill: parent
        anchors.margins: Theme.cardPadding
        spacing: Theme.gapSmall

        WorldThumbnail {
            width: parent.width
            height: Math.round((Theme.compact ? 88 : 92) * Theme.metricScale)
        }

        Column {
            id: gridDetails
            width: Math.max(0, parent.width - actionsButton.width - Theme.gapSmall)
            spacing: Theme.gapSmall

            Row {
                width: parent.width
                spacing: Theme.gapSmall
                Text {
                    text: card.title
                    color: Theme.text
                    font.bold: true
                    font.pixelSize: Theme.fontSubtitle
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, Math.max(0, parent.width - (card.xorMarked ? gridBadge.implicitWidth
                                                                                                + parent.spacing : 0)))
                }
                Text {
                    id: gridBadge
                    visible: card.xorMarked
                    text: "[XOR]"
                    color: Theme.danger
                    font.bold: true
                    font.pixelSize: Theme.fontCaption
                }
            }
            Row {
                width: parent.width
                spacing: Theme.gapSmall
                Text {
                    id: gridVersionText
                    text: card.versionText || qsTr("Unknown")
                    color: Theme.accent
                    font.pixelSize: Theme.fontCaption
                    width: Math.min(implicitWidth, Math.max(0, parent.width * 0.6))
                    elide: Text.ElideRight
                }
                Text {
                    text: "•"
                    color: Theme.muted
                    font.pixelSize: Theme.fontCaption
                }
                Text {
                    width: Math.max(0, parent.width - gridVersionText.width - parent.spacing * 2 - 8)
                    text: card.sizeText || "0 B"
                    color: Theme.muted
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                }
            }
            Text {
                width: parent.width
                text: (card.sourceText ? qsTranslate("WorldSource", card.sourceText) : qsTr("Bedrock")) + "  ·  "
                      + card.tag
                color: Theme.muted
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
        }
    }

    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton)
                cardMenu.popup()
            else
                card.openRequested(card.pathText, card.title)
        }
    }

    AppIconButton {
        id: actionsButton
        anchors.right: parent.right
        anchors.rightMargin: Theme.cardPadding
        y: card.gridMode ? gridContent.y + gridDetails.y + (gridDetails.height - height) / 2 : (card.height - height)
                           / 2
        z: 1
        iconName: "menu"
        label: qsTr("World actions")
        buttonSize: 28
        selected: cardMenu.visible
        onClicked: cardMenu.popup(actionsButton, actionsButton.width - cardMenu.width, actionsButton.height)
    }

    AppMenu {
        id: cardMenu
        AppMenuItem {
            visible: !Theme.touch
            text: qsTr("Open containing folder")
            onTriggered: if (card.pathText.length > 0)
                             App.revealPath(card.pathText)
        }
        AppMenuItem {
            enabled: !App.exportingWorld && !App.importingWorld && !App.deletingWorld
            text: qsTr("Export as mcworld")
            onTriggered: card.exportRequested(card.pathText, card.title)
        }
        AppMenuItem {
            visible: Qt.platform.os === "android"
            enabled: !App.exportingWorld && !App.importingWorld && !App.deletingWorld
            text: qsTr("Export and open")
            onTriggered: card.exportAndOpenRequested(card.pathText, card.title)
        }
        AppMenuItem {
            visible: card.appInternal
            enabled: !App.deletingWorld && !App.importingWorld && !App.exportingWorld
            danger: true
            text: qsTr("Delete world")
            onTriggered: card.deleteRequested(card.pathText, card.title)
        }
    }
}

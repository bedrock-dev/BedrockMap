import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import "controls"

AppCard {
    id: card
    interactive: true
    hovered: openButton.hovered || actionsButton.hovered || cardMenu.visible
    border.width: openButton.visualFocus ? Theme.borderWidth * 3 : Theme.borderWidth
    border.color: openButton.visualFocus ? Theme.accent : (selected || hovered ? Theme.accent : Theme.border)
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
        radius: Theme.radiusMedium
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

    component WorldDetails: Column {
        id: details
        property string title
        property string versionText
        property string tag
        property string sizeText
        property string sourceText
        property string pathText
        property bool xorMarked
        property bool compact
        property bool showPath

        readonly property string versionLabel: versionText || qsTr("Unknown version")
        readonly property string timeLabel: tag || qsTr("Unknown time")
        readonly property string sizeLabel: sizeText || "0 B"
        readonly property string sourceLabel: sourceText ? qsTranslate("WorldSource", sourceText) : qsTr("Bedrock")
        readonly property int titleSize: compact ? Theme.fontMedium : Theme.fontLarge
        readonly property int secondarySize: compact ? Theme.fontSmall : Theme.fontMedium

        spacing: Theme.gapSmall

        Row {
            width: parent.width
            spacing: Theme.gapSmall
            Text {
                readonly property real badgeSpace: details.xorMarked ? badge.implicitWidth + parent.spacing : 0
                text: details.title
                color: Theme.text
                font.pixelSize: details.titleSize
                elide: Text.ElideRight
                width: Math.min(implicitWidth, Math.max(0, parent.width - badgeSpace))
            }
            Text {
                id: badge
                visible: details.xorMarked
                text: "[XOR]"
                color: Theme.danger
                font.pixelSize: details.secondarySize
            }
        }

        Row {
            width: parent.width
            spacing: Theme.gapSmall
            Text {
                id: versionLabelText
                text: details.versionLabel
                color: Theme.accent
                font.pixelSize: details.secondarySize
                width: Math.min(implicitWidth, Math.max(0, parent.width * (details.compact ? 0.5 : 0.55)))
                elide: Text.ElideRight
            }
            Text {
                text: "•"
                color: Theme.muted
                font.pixelSize: details.secondarySize
            }
            Text {
                width: Math.max(0, parent.width - versionLabelText.width - parent.spacing * 2 - 8)
                text: details.timeLabel + " · " + details.sizeLabel
                color: Theme.muted
                font.pixelSize: details.secondarySize
                elide: Text.ElideRight
            }
        }

        Text {
            width: parent.width
            text: details.showPath && details.pathText.length > 0 ? details.sourceLabel + "  ·  " + details.pathText :
                                                                    details.sourceLabel
            color: Theme.muted
            font.pixelSize: details.secondarySize
            elide: details.compact ? Text.ElideRight : Text.ElideMiddle
        }
    }

    Row {
        visible: !card.gridMode
        anchors.fill: parent
        anchors.margins: Theme.gapLarge
        spacing: Theme.gapLarge

        WorldThumbnail {
            id: listThumbnail
            width: Theme.compact ? 64 : 76
            height: parent.height
        }

        WorldDetails {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, parent.width - listThumbnail.width - actionsButton.width - Theme.gapLarge * 2)
            title: card.title
            versionText: card.versionText
            tag: card.tag
            sizeText: card.sizeText
            sourceText: card.sourceText
            pathText: card.pathText
            xorMarked: card.xorMarked
            compact: false
            showPath: true
        }
    }

    Column {
        id: gridContent
        visible: card.gridMode
        anchors.fill: parent
        anchors.margins: Theme.gapLarge
        spacing: Theme.gapSmall

        WorldThumbnail {
            width: parent.width
            height: Math.round((Theme.compact ? 88 : 92) * Theme.metricScale)
        }

        WorldDetails {
            id: gridDetails
            width: Math.max(0, parent.width - actionsButton.width - Theme.gapSmall)
            title: card.title
            versionText: card.versionText
            tag: card.tag
            sizeText: card.sizeText
            sourceText: card.sourceText
            pathText: card.pathText
            xorMarked: card.xorMarked
            compact: true
            showPath: false
        }
    }

    AbstractButton {
        id: openButton
        anchors.fill: parent
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        background: null
        contentItem: Item {}

        Keys.onReturnPressed: animateClick()
        Keys.onEnterPressed: animateClick()
        onClicked: card.openRequested(card.pathText, card.title)

        TapHandler {
            acceptedButtons: Qt.RightButton
            acceptedDevices: PointerDevice.Mouse
            onTapped: cardMenu.popup()
        }

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        Accessible.name: card.title
        Accessible.description: qsTr("%1, %2, %3").arg(card.versionText || qsTr("Unknown version")).arg(card.sizeText
                                                                                                        || "0 B").arg(
                                    card.sourceText ? qsTranslate("WorldSource", card.sourceText) : qsTr("Bedrock"))
    }

    AppIconButton {
        id: actionsButton
        anchors.right: parent.right
        anchors.rightMargin: Theme.gapLarge
        y: card.gridMode ? gridContent.y + gridDetails.y + (gridDetails.height - height) / 2 : (card.height - height)
                           / 2
        z: 1
        iconName: "menu"
        label: qsTr("World actions")
        buttonSize: Theme.iconButtonSmallSize
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

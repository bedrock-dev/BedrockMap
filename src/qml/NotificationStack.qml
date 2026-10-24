import BedrockMap 1.0
import QtQuick
import "controls"

/// Application-wide message stack: one card per notification, bottom-centred.
///
/// It is placed above the page content — including the open-world workspace — so
/// a failure is never hidden on a page the user is not looking at, which is what
/// the worlds page's inline error banner could not do. Cards pile up from the
/// bottom edge, newest last, the way a snackbar does on desktop and on a phone.
/// The queue, the lifetimes and the folding of repeated messages belong to
/// NotificationCenter; this file only decides how a message looks.
Item {
    id: root
    /// The NotificationCenter list model.
    property var center
    readonly property int count: center ? center.count : 0
    visible: count > 0
    implicitHeight: column.implicitHeight
    height: implicitHeight

    function levelColor(level) {
        if (level === "error")
            return Theme.danger
        if (level === "warning")
            return Theme.warning
        return Theme.accent
    }

    function levelSoftColor(level) {
        if (level === "error")
            return Theme.dangerSoft
        if (level === "warning")
            return Theme.warningSoft
        return Theme.accentSoft
    }

    Column {
        id: column
        anchors.horizontalCenter: parent.horizontalCenter
        // The column is anchored by its bottom edge, so the entries grow upwards
        // and the newest card stays where the message appeared.
        anchors.bottom: parent.bottom
        width: root.width
        spacing: Theme.gapSmall

        Repeater {
            model: root.center
            delegate: Rectangle {
                id: card
                required property int index
                required property var notificationId
                required property string level
                required property string text
                required property string source
                required property string detail
                required property bool sticky
                required property int repeat

                width: column.width
                height: body.height + 20
                radius: Theme.radiusCard
                // Severity is the card's own colour — tinted fill, full-tone
                // border — rather than a bar glued to its left edge, which read
                // as a stray scrollbar. Same pairing the worlds page uses for
                // the messages it shows inline.
                color: root.levelSoftColor(card.level)
                border.color: root.levelColor(card.level)
                border.width: 1

                // Only the newest card animates in: the ones above it were
                // already on screen and should not flicker when a card leaves.
                // Assigned imperatively so a later row removal (which changes the
                // index of every card above) can never send this back to 0 and
                // hide a card that is already visible.
                property real entrance: 1
                opacity: entrance
                transform: Translate {
                    y: (1 - card.entrance) * 14
                }
                NumberAnimation {
                    id: entranceAnim
                    target: card
                    property: "entrance"
                    from: 0
                    to: 1
                    duration: 170
                    easing.type: Easing.OutCubic
                }
                Component.onCompleted: {
                    if (card.index === root.count - 1) {
                        card.entrance = 0
                        entranceAnim.start()
                    }
                }

                // Clicking anywhere dismisses the card, so the × is only the
                // visible affordance. Declared first: the close button and the
                // text are declared later and therefore stay on top of it.
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.center.dismiss(card.notificationId)
                }

                AppIcon {
                    id: dismissButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.rightMargin: 10
                    width: 16
                    height: 16
                    name: "close"
                    iconSize: width
                    iconColor: (dismissArea.pressed || dismissArea.containsMouse) ? Theme.danger : Theme.muted
                    MouseArea {
                        id: dismissArea
                        anchors.fill: parent
                        anchors.margins: -6
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.center.dismiss(card.notificationId)
                    }
                }

                Column {
                    id: body
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.right: parent.right
                    anchors.rightMargin: 30
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3

                    // Header line: where the message came from and how often it
                    // repeated. The whole row is hidden — not just its texts —
                    // when there is nothing to show: a Column only skips items
                    // that are invisible themselves, so an always-visible row
                    // holding an invisible label would still reserve its line and
                    // leave a blank strip at the top of the card.
                    Row {
                        width: parent.width
                        visible: card.source.length > 0 || card.repeat > 1
                        spacing: 6
                        Text {
                            id: header
                            text: card.source
                            visible: card.source.length > 0
                            color: root.levelColor(card.level)
                            font.pixelSize: Theme.fontBody
                            font.bold: true
                            elide: Text.ElideRight
                            width: Math.min(implicitWidth, body.width - repeatBadge.width - 6)
                        }
                        Text {
                            id: repeatBadge
                            text: qsTr("×%1").arg(card.repeat)
                            visible: card.repeat > 1
                            color: Theme.muted
                            font.pixelSize: Theme.fontBody
                        }
                    }

                    Text {
                        width: parent.width
                        text: card.text
                        color: Theme.text
                        // One tier above the metadata lines and still inside the
                        // theme's ladder, so the message reads as the content of
                        // the card without shouting.
                        font.pixelSize: Theme.fontSubtitle
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        width: parent.width
                        text: card.detail
                        visible: card.detail.length > 0
                        color: Theme.muted
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideMiddle
                    }
                }
            }
        }
    }
}

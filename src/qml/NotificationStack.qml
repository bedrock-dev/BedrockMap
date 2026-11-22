import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
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
    readonly property real maximumHeight: Math.max(Theme.ui(120), Math.min(Theme.ui(320), parent ? parent.height * 0.5 :
                                                                                                   Theme.ui(320)))
    visible: count > 0
    implicitHeight: Math.min(column.implicitHeight, maximumHeight)
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

    Flickable {
        id: viewport
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, column.implicitHeight)
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds

        function scrollToEnd() {
            Qt.callLater(function () {
                contentY = Math.max(0, contentHeight - height)
            })
        }

        onContentHeightChanged: scrollToEnd()
        onHeightChanged: scrollToEnd()
        Component.onCompleted: scrollToEnd()

        ScrollBar.vertical: AppScrollBar {}

        Column {
            id: column
            // Keep short stacks bottom-aligned while allowing taller stacks to
            // scroll without extending over the application top bar.
            width: viewport.width
            y: Math.max(0, viewport.height - implicitHeight)
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
                    height: body.height + Theme.gapLarge * 2
                    radius: Theme.radiusLarge
                    // Severity is the card's own colour — tinted fill, full-tone
                    // border — rather than a bar glued to its left edge, which read
                    // as a stray scrollbar. Same pairing the worlds page uses for
                    // the messages it shows inline.
                    color: root.levelSoftColor(card.level)
                    border.color: root.levelColor(card.level)
                    border.width: Theme.borderWidth

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

                    // Clicking anywhere dismisses the card. The visible close
                    // button remains a separate focusable control.
                    AbstractButton {
                        anchors.fill: parent
                        focusPolicy: Qt.TabFocus
                        background: null
                        contentItem: Item {}
                        Keys.onReturnPressed: animateClick()
                        Keys.onEnterPressed: animateClick()
                        onClicked: root.center.dismiss(card.notificationId)
                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }
                        Accessible.name: card.text
                        Accessible.description: card.detail
                    }

                    AppIconButton {
                        id: dismissButton
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: Theme.gapSmall
                        buttonSize: Theme.iconButtonSmallSize
                        iconName: "close"
                        label: qsTr("Close")
                        danger: true
                        onClicked: root.center.dismiss(card.notificationId)
                    }

                    Column {
                        id: body
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.gapLarge
                        anchors.right: parent.right
                        anchors.rightMargin: dismissButton.width + Theme.gapMedium
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.gapTiny

                        // Header line: where the message came from and how often it
                        // repeated. The whole row is hidden — not just its texts —
                        // when there is nothing to show: a Column only skips items
                        // that are invisible themselves, so an always-visible row
                        // holding an invisible label would still reserve its line and
                        // leave a blank strip at the top of the card.
                        Row {
                            width: parent.width
                            visible: card.source.length > 0 || card.repeat > 1
                            spacing: Theme.gapMedium
                            Text {
                                id: header
                                text: card.source
                                visible: card.source.length > 0
                                color: root.levelColor(card.level)
                                font.pixelSize: Theme.fontMedium
                                elide: Text.ElideRight
                                width: Math.min(implicitWidth, body.width - repeatBadge.width - Theme.gapMedium)
                            }
                            Text {
                                id: repeatBadge
                                text: qsTr("×%1").arg(card.repeat)
                                visible: card.repeat > 1
                                color: Theme.muted
                                font.pixelSize: Theme.fontMedium
                            }
                        }

                        Text {
                            width: parent.width
                            text: card.text
                            color: Theme.text
                            // One tier above the metadata lines and still inside the
                            // theme's ladder, so the message reads as the content of
                            // the card without shouting.
                            font.pixelSize: Theme.fontMedium
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            width: parent.width
                            text: card.detail
                            visible: card.detail.length > 0
                            color: Theme.muted
                            font.pixelSize: Theme.fontMedium
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }
        }
    }

    onCountChanged: viewport.scrollToEnd()
}

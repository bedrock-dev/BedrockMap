import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    property string message
    property string confirmText: qsTr("OK")
    property string cancelText: qsTr("Cancel")
    property bool danger: false
    signal confirmed
    signal cancelled

    property bool confirmPending: false

    modal: true
    parent: Overlay.overlay
    closePolicy: Popup.CloseOnEscape
    width: Math.min(430, parent ? parent.width - 40 : 430)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: Theme.cardPadding

    function accept() {
        confirmPending = true
        close()
        confirmed()
    }

    onClosed: {
        if (!confirmPending)
            cancelled()
        confirmPending = false
    }

    background: Rectangle {
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: Theme.border
    }

    header: Item {
        implicitHeight: Theme.controlHeight + Theme.gapSmall
        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.gapSmall
            anchors.right: parent.right
            anchors.rightMargin: Theme.gapSmall
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.fontLabel
            font.bold: true
            elide: Text.ElideRight
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.gapMedium
        Text {
            Layout.fillWidth: true
            text: root.message
            color: Theme.text
            font.pixelSize: Theme.fontSubtitle
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: root.cancelText
                onClicked: root.close()
            }
            AppButton {
                text: root.confirmText
                primary: !root.danger
                danger: root.danger
                onClicked: root.accept()
            }
        }
    }
}

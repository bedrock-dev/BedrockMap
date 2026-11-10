import BedrockMap 1.0
import QtQuick
import QtQuick.Layouts

AppDialog {
    id: root
    property string message
    property string confirmText: qsTr("OK")
    property string cancelText: qsTr("Cancel")
    property bool danger: false
    signal confirmed
    signal cancelled

    property bool confirmPending: false

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
            spacing: Theme.gapMedium
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

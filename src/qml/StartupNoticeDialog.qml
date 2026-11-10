import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

AppDialog {
    id: root
    title: qsTr("Before you start")
    closePolicy: Popup.NoAutoClose
    preferredWidth: Theme.ui(600)

    contentItem: ColumnLayout {
        spacing: Theme.gapMedium
        ScrollView {
            id: noticeScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.preferredHeight: noticeContent.implicitHeight
            contentWidth: availableWidth
            contentHeight: noticeContent.implicitHeight
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                id: noticeContent
                width: noticeScroll.availableWidth
                spacing: Theme.gapLarge
                Repeater {
                    model: [
                        qsTr("Editing worlds carries a risk of data loss or corruption. The developer accepts no responsibility for any resulting world damage or data loss. Back up important worlds before use and keep your backups up to date."),
                        Qt.platform.os === "android" ? qsTr("Due to Android's storage restrictions, access to shared storage requires the All files access permission. Open Tools > Permissions in this app and request it manually, then enable it in system settings.") : "",
                        qsTr("BedrockMap is an independent, unofficial tool. It is not affiliated with, endorsed by, or associated with Mojang or Minecraft.")
                    ].filter(paragraph => paragraph.length > 0)
                    delegate: Text {
                        required property string modelData
                        Layout.fillWidth: true
                        text: modelData
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.pixelSize: Theme.fontSubtitle
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
        AppButton {
            Layout.alignment: Qt.AlignRight
            text: qsTr("I understand")
            primary: true
            onClicked: root.accept()
        }
    }
}

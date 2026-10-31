import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

Dialog {
    id: root
    property string newVersion
    property string releaseNotes
    property string releaseUrl
    readonly property string localizedNotes: selectReleaseNotes(releaseNotes)

    title: qsTr("Update available")
    modal: true
    parent: Overlay.overlay
    closePolicy: Popup.CloseOnEscape
    width: Math.min(640, parent ? parent.width - 32 : 640)
    height: Math.min(560, parent ? parent.height - 48 : 560)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: Theme.cardPadding

    function present(version, notes, url) {
        newVersion = version
        releaseNotes = notes
        releaseUrl = url
        open()
    }

    function selectReleaseNotes(notes) {
        const lines = notes.split(/\r?\n/)
        const separator = lines.findIndex(line => line.trim() === "---")
        if (separator < 0)
            return notes.trim()
        // Legacy release notes contain Chinese first, then English after "---".
        const configured = App.settings.language
        const language = !configured || configured === "system" ? Qt.locale().name : configured
        const selected = language.toLowerCase().startsWith("zh") ? lines.slice(0, separator) : lines.slice(separator + 1)
        return selected.join("\n").trim() || notes.trim()
    }

    background: Rectangle {
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
    }
    header: Item {
        implicitHeight: Theme.controlHeight + Theme.gapSmall
        Text {
            anchors.fill: parent
            anchors.leftMargin: root.padding
            anchors.rightMargin: root.padding
            verticalAlignment: Text.AlignVCenter
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
            text: qsTr("New version: %1\nCurrent version: %2").arg(root.newVersion).arg(Qt.application.version)
            textFormat: Text.PlainText
            color: Theme.text
            font.pixelSize: Theme.fontSubtitle
            wrapMode: Text.Wrap
        }
        ScrollView {
            id: notesScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.preferredHeight: 0
            contentWidth: availableWidth
            contentHeight: notesText.implicitHeight
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            AppTextArea {
                id: notesText
                width: notesScroll.availableWidth
                implicitHeight: Math.max(notesScroll.availableHeight, contentHeight + topPadding + bottomPadding)
                text: root.localizedNotes || qsTr("No release notes provided.")
                textFormat: TextEdit.MarkdownText
                readOnly: true
                selectByMouse: !Theme.touch
                onLinkActivated: link => Qt.openUrlExternally(link)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapSmall
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: qsTr("Later")
                onClicked: root.close()
            }
            AppButton {
                text: qsTr("Download")
                primary: true
                enabled: root.releaseUrl.length > 0
                onClicked: {
                    Qt.openUrlExternally(root.releaseUrl)
                    root.close()
                }
            }
        }
    }
}

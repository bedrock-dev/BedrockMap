import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BedrockMap 1.0
import "controls"

Item {
    id: page

    component ShortcutSection: AppCard {
        id: shortcutSection
        property string heading
        property var entries

        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        implicitHeight: content.implicitHeight + Theme.gapLarge * 2

        Column {
            id: content
            anchors.fill: parent
            anchors.margins: Theme.gapLarge
            spacing: Theme.gapSmall

            Text {
                text: shortcutSection.heading
                color: Theme.text
                font.pixelSize: Theme.fontLarge
            }
            Repeater {
                model: shortcutSection.entries
                delegate: RowLayout {
                    required property var modelData

                    width: content.width
                    spacing: Theme.gapSmall

                    Text {
                        Layout.fillWidth: true
                        text: modelData.description
                        color: Theme.muted
                        font.pixelSize: Theme.fontMedium
                        wrapMode: Text.WordWrap
                    }
                    Rectangle {
                        implicitWidth: shortcutLabel.implicitWidth + Theme.gapMedium
                        implicitHeight: shortcutLabel.implicitHeight + Theme.gapSmall
                        radius: Theme.radiusSmall
                        color: Theme.surfaceAlt

                        Text {
                            id: shortcutLabel
                            anchors.centerIn: parent
                            text: modelData.sequence
                            color: Theme.accent
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                }
            }
        }
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: body.implicitHeight + Theme.gapSection * 2
        clip: true
        ScrollBar.vertical: AppScrollBar {}

        Column {
            id: body
            x: Theme.gapSection
            y: Theme.gapSection
            width: Math.max(0, page.width - Theme.gapSection * 2)
            spacing: Theme.gapSection

            Column {
                width: parent.width
                spacing: Theme.gapSmall

                Text {
                    width: parent.width
                    text: qsTr("Available shortcuts for the current Qt Quick interface.")
                    color: Theme.muted
                    font.pixelSize: Theme.fontMedium
                    wrapMode: Text.WordWrap
                }
            }

            RowLayout {
                width: parent.width
                spacing: Theme.gapLarge

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gapLarge

                    ShortcutSection {
                        heading: qsTr("Global")
                        entries: ShortcutRegistry.app
                    }
                    ShortcutSection {
                        heading: qsTr("Selection and chunks")
                        entries: ShortcutRegistry.chunk
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gapLarge

                    ShortcutSection {
                        heading: qsTr("Map view")
                        entries: ShortcutRegistry.map
                    }
                    ShortcutSection {
                        heading: qsTr("3D view")
                        entries: ShortcutRegistry.voxel
                    }
                }
            }
        }
    }
}

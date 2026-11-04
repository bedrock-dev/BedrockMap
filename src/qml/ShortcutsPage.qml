import QtQuick
import QtQuick.Layouts
import BedrockMap 1.0
import "controls"

Item {
    id: page

    readonly property var globalShortcuts: [
        {
            description: qsTr("Open world"),
            sequence: "Ctrl+O"
        },
        {
            description: qsTr("Open file"),
            sequence: "Ctrl+Shift+O"
        },
        {
            description: qsTr("New NBT document"),
            sequence: "Ctrl+N"
        },
        {
            description: qsTr("Save active document"),
            sequence: "Ctrl+S"
        },
        {
            description: qsTr("Close active session"),
            sequence: "Ctrl+W"
        },
        {
            description: qsTr("Exit BedrockMap"),
            sequence: "Ctrl+Q"
        },
        {
            description: qsTr("Open settings"),
            sequence: "Ctrl+,"
        }
    ]
    readonly property var mapShortcuts: [
        {
            description: qsTr("Toggle grid"),
            sequence: "Alt+G"
        },
        {
            description: qsTr("Toggle coordinates"),
            sequence: "Alt+C"
        },
        {
            description: qsTr("Overworld"),
            sequence: "Alt+1"
        },
        {
            description: qsTr("Nether"),
            sequence: "Alt+2"
        },
        {
            description: qsTr("The End"),
            sequence: "Alt+3"
        },
        {
            description: qsTr("Terrain layer"),
            sequence: "Alt+T"
        },
        {
            description: qsTr("Biome layer"),
            sequence: "Alt+B"
        },
        {
            description: qsTr("Toggle slime chunks"),
            sequence: "Alt+S"
        },
        {
            description: qsTr("Toggle entities"),
            sequence: "Alt+A"
        },
        {
            description: qsTr("Toggle villages"),
            sequence: "Alt+V"
        },
        {
            description: qsTr("Toggle HSA"),
            sequence: "Alt+H"
        },
        {
            description: qsTr("Toggle transparent void"),
            sequence: "Alt+0"
        },
        {
            description: qsTr("Toggle map filters"),
            sequence: "Alt+F"
        },
        {
            description: qsTr("Toggle debug panels"),
            sequence: "Alt+D"
        }
    ]
    readonly property var chunkShortcuts: [
        {
            description: qsTr("Replace selection mode"),
            sequence: "Shift+R"
        },
        {
            description: qsTr("Add selection mode"),
            sequence: "Shift+A"
        },
        {
            description: qsTr("Subtract selection mode"),
            sequence: "Shift+S"
        },
        {
            description: qsTr("Clear selection"),
            sequence: "Shift+C"
        },
        {
            description: qsTr("Copy selected chunks"),
            sequence: "Ctrl+C"
        },
        {
            description: qsTr("Paste chunks"),
            sequence: "Ctrl+V"
        },
        {
            description: qsTr("Export selected chunks"),
            sequence: "Ctrl+E"
        },
        {
            description: qsTr("Import chunks"),
            sequence: "Ctrl+I"
        },
        {
            description: qsTr("Delete selected chunks"),
            sequence: "Ctrl+D"
        },
        {
            description: qsTr("Clear selected chunks"),
            sequence: "Ctrl+K"
        },
        {
            description: qsTr("Edit selection biome"),
            sequence: "Ctrl+B"
        },
        {
            description: qsTr("Save map screenshot"),
            sequence: "Ctrl+T"
        },
        {
            description: qsTr("Open 3D view"),
            sequence: "Ctrl+H"
        },
        {
            description: qsTr("Go to coordinates"),
            sequence: "Ctrl+G"
        }
    ]
    readonly property var voxelShortcuts: [
        {
            description: qsTr("Reset camera"),
            sequence: "R"
        },
        {
            description: qsTr("Face front"),
            sequence: "F"
        },
        {
            description: qsTr("Toggle orthographic projection"),
            sequence: "O"
        },
        {
            description: qsTr("Toggle axes"),
            sequence: "A"
        },
        {
            description: qsTr("Toggle rotation lock"),
            sequence: "L"
        },
        {
            description: qsTr("Toggle selection"),
            sequence: "S"
        },
        {
            description: qsTr("Toggle selection move mode"),
            sequence: "M"
        },
        {
            description: qsTr("Rotate view with arrow keys"),
            sequence: "↑ ↓ ← →"
        }
    ]

    component ShortcutSection: AppCard {
        id: shortcutSection
        property string heading
        property var entries

        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        implicitHeight: content.implicitHeight + Theme.cardPadding * 2

        Column {
            id: content
            anchors.fill: parent
            anchors.margins: Theme.cardPadding
            spacing: Theme.gapSmall

            Text {
                text: shortcutSection.heading
                color: Theme.text
                font.pixelSize: Theme.fontLabel
                font.bold: true
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
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.WordWrap
                    }
                    Rectangle {
                        implicitWidth: shortcutLabel.implicitWidth + Theme.ui(8)
                        implicitHeight: shortcutLabel.implicitHeight + Theme.ui(4)
                        radius: Theme.radiusSmall
                        color: Theme.surfaceAlt

                        Text {
                            id: shortcutLabel
                            anchors.centerIn: parent
                            text: modelData.sequence
                            color: Theme.accent
                            font.pixelSize: Theme.fontCaption
                            font.bold: true
                        }
                    }
                }
            }
        }
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: body.implicitHeight + Theme.pageGutter * 2
        clip: true

        Column {
            id: body
            x: Theme.pageGutter
            y: Theme.pageGutter
            width: Math.max(0, page.width - Theme.pageGutter * 2)
            spacing: Theme.gapSection

            Column {
                width: parent.width
                spacing: Theme.gapSmall

                Text {
                    text: qsTr("Keyboard shortcuts")
                    color: Theme.text
                    font.pixelSize: Theme.fontHeading
                    font.bold: true
                }
                Text {
                    width: parent.width
                    text: qsTr("Available shortcuts for the current Qt Quick interface.")
                    color: Theme.muted
                    font.pixelSize: Theme.fontSubtitle
                    wrapMode: Text.WordWrap
                }
            }

            RowLayout {
                width: parent.width
                spacing: Theme.cardGap

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.cardGap

                    ShortcutSection {
                        heading: qsTr("Global")
                        entries: page.globalShortcuts
                    }
                    ShortcutSection {
                        heading: qsTr("Selection and chunks")
                        entries: page.chunkShortcuts
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.cardGap

                    ShortcutSection {
                        heading: qsTr("Map view")
                        entries: page.mapShortcuts
                    }
                    ShortcutSection {
                        heading: qsTr("3D view")
                        entries: page.voxelShortcuts
                    }
                }
            }
        }
    }
}

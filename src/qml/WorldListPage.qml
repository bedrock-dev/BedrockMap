import BedrockMap 1.0
import QtCore
import QtQuick
import QtQuick.Dialogs
import "controls"

Item {
    id: page
    signal openWorld(string path, string title)
    signal addRequested
    signal newNbtRequested
    property bool tableMode: false
    readonly property bool listOnly: !Theme.showWorldCards
    readonly property bool effectiveTableMode: listOnly || tableMode
    property var recentWorlds: App.worlds ? App.worlds.recentWorlds : []
    property var allWorlds: App.worlds ? App.worlds.allWorlds : []
    readonly property var sourceOptions: App.worlds ? App.worlds.sourceOptions : []
    readonly property var sourceLabels: {
        const labels = [qsTr("All")]
        for (let i = 0; i < sourceOptions.length; ++i)
            labels.push(qsTranslate("WorldSource", sourceOptions[i]))
        return labels
    }

    function confirmWorldDeletion(path, title) {
        deleteWorldDialog.worldPath = path
        deleteWorldDialog.worldTitle = title
        deleteWorldDialog.open()
    }

    function openFile() {
        openFileDialog.open()
    }

    function chooseWorldExport(path, title) {
        exportWorldDialog.worldPath = path
        const fileName = title.replace(/[<>:"/\\|?*\u0000-\u001f]/g, "_").replace(/[. ]+$/, "") || "world"
        exportWorldDialog.selectedFile = exportWorldDialog.currentFolder + "/" + encodeURIComponent(fileName)
                + ".mcworld"
        exportWorldDialog.open()
    }

    FileUrlDialog {
        id: exportWorldDialog
        property string worldPath
        title: qsTr("Export as mcworld")
        fileMode: FileDialog.SaveFile
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        defaultSuffix: "mcworld"
        nameFilters: [qsTr("Minecraft worlds (*.mcworld)")]
        onAccepted: {
            const path = worldPath
            worldPath = ""
            App.exportWorld(path, selectedUrl)
        }
        onRejected: worldPath = ""
    }

    ConfirmDialog {
        id: deleteWorldDialog
        property string worldPath
        property string worldTitle
        title: qsTr("Delete world")
        message: qsTr('Permanently delete "%1" from app storage? This cannot be undone.').arg(worldTitle) + "\n\n"
                 + worldPath
        confirmText: qsTr("Delete")
        danger: true
        onConfirmed: {
            const path = worldPath
            worldPath = ""
            App.deleteWorld(path)
        }
        onCancelled: worldPath = ""
    }

    FileUrlDialog {
        id: openFileDialog
        title: qsTr("Open file")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Minecraft files (*.mcworld *.nbt *.nbts *.mcstructure *.palette)"), qsTr("All files (*)")]
        onAccepted: App.openFile(selectedUrl)
    }

    component SectionTitle: Row {
        property string title
        property int count
        spacing: Theme.gapSmall
        Text {
            id: titleText
            text: parent.title
            color: Theme.text
            font.pixelSize: Theme.fontLabel
        }
        Text {
            anchors.baseline: titleText.baseline
            text: parent.count
            color: Theme.muted
            font.pixelSize: Theme.fontBody
        }
    }

    component WorldSection: Column {
        id: section
        property string title
        property var worlds
        property bool sourceFiltering: false
        readonly property int count: page.effectiveTableMode ? listView.count : gridView.count
        width: parent.width
        spacing: Theme.gapMedium

        Item {
            width: parent.width
            height: Math.max(sectionTitle.height, sourceSelector.visible ? sourceSelector.height : 0)
            SectionTitle {
                id: sectionTitle
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                title: section.title
                count: section.count
            }
            AppSegmentedControl {
                id: sourceSelector
                visible: section.sourceFiltering
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, Math.min(implicitWidth, parent.width - sectionTitle.width - Theme.gapMedium))
                fitLabels: true
                options: page.sourceLabels
                currentIndex: App.worlds ? Math.max(0, page.sourceOptions.indexOf(App.worlds.sourceFilter) + 1) : 0
                onActivated: index => {
                    if (App.worlds)
                        App.worlds.sourceFilter = index === 0 ? "" : page.sourceOptions[index - 1]
                }
            }
        }

        GridView {
            id: gridView
            visible: !page.effectiveTableMode
            width: parent.width
            height: count > 0 ? contentHeight : 42
            interactive: false
            cellWidth: Math.max(1, width / Math.max(1, Math.floor(width / Math.round(230 * Theme.metricScale))))
            cellHeight: Math.round((Theme.compact ? 166 : 178) * Theme.metricScale)
            model: section.worlds
            delegate: WorldCardDelegate {
                width: GridView.view.cellWidth
                height: GridView.view.cellHeight
                gridMode: true
                onOpenRequested: (worldPath, worldTitle) => page.openWorld(worldPath, worldTitle)
                onDeleteRequested: (worldPath, worldTitle) => page.confirmWorldDeletion(worldPath, worldTitle)
                onExportRequested: (worldPath, worldTitle) => page.chooseWorldExport(worldPath, worldTitle)
                onExportAndOpenRequested: (worldPath, worldTitle) => App.exportAndOpenWorld(worldPath, worldTitle)
            }
        }

        ListView {
            id: listView
            visible: page.effectiveTableMode
            width: parent.width
            height: count > 0 ? contentHeight : 42
            interactive: false
            spacing: Theme.gapLarge
            model: section.worlds
            delegate: WorldCardDelegate {
                width: ListView.view.width
                height: Math.round((Theme.compact ? 76 : 92) * Theme.metricScale)
                gridMode: false
                onOpenRequested: (worldPath, worldTitle) => page.openWorld(worldPath, worldTitle)
                onDeleteRequested: (worldPath, worldTitle) => page.confirmWorldDeletion(worldPath, worldTitle)
                onExportRequested: (worldPath, worldTitle) => page.chooseWorldExport(worldPath, worldTitle)
                onExportAndOpenRequested: (worldPath, worldTitle) => App.exportAndOpenWorld(worldPath, worldTitle)
            }
            Text {
                visible: listView.count === 0
                anchors.centerIn: parent
                text: qsTr("No worlds")
                color: Theme.muted
                font.pixelSize: Theme.fontBody
            }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: body.height + Theme.gapSection
        clip: true
        Column {
            id: body
            x: Theme.gapSection
            y: Theme.gapMedium
            width: parent.width - Theme.gapSection * 2
            spacing: Theme.gapMedium

            Text {
                text: qsTr("Getting Started")
                color: Theme.text
                font.pixelSize: Theme.fontDisplay
            }
            Flow {
                id: welcomeActions
                width: parent.width
                spacing: Theme.gapMedium
                readonly property real actionWidth: (width - spacing * 2) / 3
                AppListRow {
                    width: welcomeActions.actionWidth
                    height: Theme.touch ? implicitHeight : (Theme.compact ? 64 : 68)
                    stacked: Theme.compact
                    iconName: "opened_world_list"
                    label: qsTr("Open world folder")
                    detail: Theme.compact ? "" : qsTr("Open a Bedrock world folder")
                    onClicked: page.addRequested()
                }
                AppListRow {
                    width: welcomeActions.actionWidth
                    height: Theme.touch ? implicitHeight : (Theme.compact ? 64 : 68)
                    stacked: Theme.compact
                    iconName: "open"
                    label: qsTr("Open file")
                    detail: Theme.compact ? "" : qsTr("Open NBT, mcstructure, or mcworld files")
                    onClicked: page.openFile()
                }
                AppListRow {
                    width: welcomeActions.actionWidth
                    height: Theme.touch ? implicitHeight : (Theme.compact ? 64 : 68)
                    stacked: Theme.compact
                    iconName: "nbt_editor"
                    label: qsTr("New NBT document")
                    detail: Theme.compact ? "" : qsTr("Create an empty NBT document")
                    onClicked: page.newNbtRequested()
                }
            }

            Item {
                width: parent.width
                height: Math.max(worldsTitle.implicitHeight, worldsActions.height)
                Text {
                    id: worldsTitle
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Worlds")
                    color: Theme.text
                    font.pixelSize: Theme.fontHeading
                }
                Row {
                    id: worldsActions
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.gapSmall
                    AppSegmentedControl {
                        id: viewMode
                        visible: Theme.showWorldCards
                        fitLabels: true
                        options: [qsTr("Cards"), qsTr("List")]
                        currentIndex: page.effectiveTableMode ? 1 : 0
                        onActivated: index => page.tableMode = index === 1
                    }
                    AppIconButton {
                        id: refreshButton
                        iconName: "refresh"
                        label: qsTr("Refresh worlds")
                        buttonSize: Theme.iconButtonSmallSize
                        onClicked: App.worlds.refresh()
                    }
                }
            }

            WorldSection {
                id: recentSection
                visible: count > 0
                title: qsTr("Recent worlds")
                worlds: page.recentWorlds
            }

            WorldSection {
                id: worldsSection
                visible: page.sourceOptions.length > 0
                title: qsTr("All worlds")
                worlds: page.allWorlds
                sourceFiltering: true
            }

            Rectangle {
                visible: page.sourceOptions.length === 0 && recentSection.count === 0 && !(App.worlds
                                                                                           && App.worlds.loading)
                width: parent.width
                height: 62
                radius: Theme.radiusCard
                color: emptyMouse.pressed ? Theme.surfaceAlt : Theme.surface
                border.color: Theme.border
                Row {
                    anchors.fill: parent
                    anchors.margins: Theme.gapMedium
                    spacing: Theme.gapLarge
                    Rectangle {
                        width: 74
                        height: 42
                        radius: Theme.radiusSmall
                        color: Theme.accentSoft
                        AppIcon {
                            anchors.centerIn: parent
                            width: 22
                            height: 22
                            name: "world_list"
                            iconSize: width
                            iconColor: Theme.accent
                        }
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.gapSmall
                        Text {
                            text: qsTr("No worlds yet")
                            color: Theme.text
                            font.pixelSize: Theme.fontSubtitle
                        }
                        Text {
                            text: qsTr("Open a Bedrock world folder to add one")
                            color: Theme.muted
                            font.pixelSize: Theme.fontBody
                        }
                    }
                }
                MouseArea {
                    id: emptyMouse
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.addRequested()
                }
            }
        }
    }
}

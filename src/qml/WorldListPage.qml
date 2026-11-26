import BedrockMap 1.0
import QtCore
import QtQuick
import QtQuick.Controls
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
        const fileName = FileDialogSupport.sanitizedFileName(title, "world")
        exportWorldDialog.selectedFile = FileDialogSupport.selectedUrlWithSuffix(exportWorldDialog, fileName, "mcworld")
        exportWorldDialog.open()
    }

    function chooseWorldFolderExport(path, title) {
        exportFolderDialog.worldPath = path
        exportFolderDialog.worldTitle = title
        exportFolderDialog.open()
    }

    FileUrlDialog {
        id: exportWorldDialog
        property string worldPath
        title: qsTr("Export as mcworld")
        fileMode: FileDialog.SaveFile
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        defaultSuffix: "mcworld"
        nameFilters: FileDialogSupport.mcworldFilters
        onAccepted: {
            const path = worldPath
            worldPath = ""
            App.exportWorld(path, selectedUrl)
        }
        onRejected: worldPath = ""
    }

    FolderUrlDialog {
        id: exportFolderDialog
        property string worldPath
        property string worldTitle
        title: qsTr("Choose export folder")
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            const path = worldPath
            const title = worldTitle
            const destination = selectedUrl
            worldPath = ""
            worldTitle = ""
            if (destination.toString().length === 0)
                return
            const folderState = App.exportFolderState(destination)
            if (folderState === "empty") {
                App.exportWorldFolder(path, destination)
                return
            }
            if (folderState === "unavailable") {
                App.notifications.error(qsTr("Could not inspect the selected folder."), destination.toString())
                return
            }
            exportFolderConfirm.worldPath = path
            exportFolderConfirm.worldTitle = title
            exportFolderConfirm.destination = destination
            exportFolderConfirm.open()
        }
        onRejected: {
            worldPath = ""
            worldTitle = ""
        }
    }

    ConfirmDialog {
        id: exportFolderConfirm
        property string worldPath
        property string worldTitle
        property url destination
        title: qsTr("Export to a non-empty folder?")
        message: qsTr(
                     'Export "%1" into the selected folder? The folder is not empty. Existing files with matching names will be replaced; other files will remain.\n\n%2\n\nContinue?').arg(
                     worldTitle).arg(destination.toString())
        confirmText: qsTr("Export")
        danger: true
        onConfirmed: {
            const path = worldPath
            const target = destination
            worldPath = ""
            worldTitle = ""
            destination = ""
            App.exportWorldFolder(path, target)
        }
        onCancelled: {
            worldPath = ""
            worldTitle = ""
            destination = ""
        }
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
        nameFilters: FileDialogSupport.minecraftFileFilters
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
            font.pixelSize: Theme.fontLarge
        }
        Text {
            anchors.baseline: titleText.baseline
            text: parent.count
            color: Theme.muted
            font.pixelSize: Theme.fontMedium
        }
    }

    component WorldDelegate: WorldCardDelegate {
        onOpenRequested: (worldPath, worldTitle) => page.openWorld(worldPath, worldTitle)
        onDeleteRequested: (worldPath, worldTitle) => page.confirmWorldDeletion(worldPath, worldTitle)
        onExportRequested: (worldPath, worldTitle) => page.chooseWorldExport(worldPath, worldTitle)
        onExportFolderRequested: (worldPath, worldTitle) => page.chooseWorldFolderExport(worldPath, worldTitle)
        onExportAndOpenRequested: (worldPath, worldTitle) => App.exportAndOpenWorld(worldPath, worldTitle)
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
            delegate: WorldDelegate {
                width: GridView.view.cellWidth
                height: GridView.view.cellHeight
                gridMode: true
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
            delegate: WorldDelegate {
                width: ListView.view.width
                height: Math.round((Theme.compact ? 76 : 92) * Theme.metricScale)
                gridMode: false
            }
            Text {
                visible: listView.count === 0
                anchors.centerIn: parent
                text: qsTr("No worlds")
                color: Theme.muted
                font.pixelSize: Theme.fontMedium
            }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: body.height + Theme.gapSection
        clip: true
        ScrollBar.vertical: AppScrollBar {}
        Column {
            id: body
            x: Theme.compact ? Theme.gapMedium : Theme.gapSection
            y: Theme.gapMedium
            width: parent.width - body.x * 2
            spacing: Theme.gapMedium

            Text {
                text: qsTr("Getting Started")
                color: Theme.text
                font.pixelSize: Theme.fontHuge
            }
            Row {
                id: welcomeActions
                width: parent.width
                spacing: Theme.gapMedium
                readonly property real minimumActionWidth: Theme.ui(180)
                readonly property int actionColumns: {
                    if (Theme.compact)
                        return 3
                    return Math.max(1, Math.min(3, Math.floor((width + spacing) / (minimumActionWidth + spacing))))
                }
                readonly property real actionWidth: (width - spacing * (actionColumns - 1)) / actionColumns
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
                    iconName: "new_file"
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
                    font.pixelSize: Theme.fontHuge
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
                radius: Theme.radiusLarge
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
                            font.pixelSize: Theme.fontMedium
                        }
                        Text {
                            text: qsTr("Open a Bedrock world folder to add one")
                            color: Theme.muted
                            font.pixelSize: Theme.fontMedium
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

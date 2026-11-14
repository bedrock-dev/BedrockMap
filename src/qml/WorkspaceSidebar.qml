import BedrockMap 1.0
import QtQuick
import "controls"

// Desktop navigation rail: map, child pages, global NBT sections, save and close.
Rectangle {
    id: sidebar

    property var session: null
    property var globalSections: []
    property int section: 0
    property bool chunkEditorOpen: false
    property bool voxelViewOpen: false

    signal sectionRequested(int section)
    signal chunkEditorToggleRequested
    signal voxelViewToggleRequested
    signal saveRequested
    signal closeRequested

    width: Theme.sidebarCollapsedWidth
    color: Theme.surface
    border.width: 0

    Rectangle {
        width: Theme.borderWidth
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        color: Theme.border
    }
    Flickable {
        width: parent.width
        anchors.top: parent.top
        anchors.topMargin: Theme.gapMedium
        anchors.bottom: footer.top
        contentWidth: width
        contentHeight: navigationColumn.height + Theme.gapMedium * 2
        clip: true
        Column {
            id: navigationColumn
            x: Theme.gapMedium
            y: Theme.gapMedium
            width: parent.width - Theme.gapMedium * 2
            spacing: Theme.gapTiny
            AppListRow {
                iconName: "map"
                label: qsTr("Map")
                compact: true
                selected: sidebar.section === 0
                onClicked: sidebar.sectionRequested(0)
            }
            AppListRow {
                iconName: "nbt_editor"
                label: qsTr("Chunk editor")
                dirty: !!sidebar.session && sidebar.session.chunkEditor.dirty
                compact: true
                selected: sidebar.chunkEditorOpen
                onClicked: sidebar.chunkEditorToggleRequested()
            }
            AppListRow {
                iconName: "structure"
                label: qsTr("3D view")
                compact: true
                selected: sidebar.voxelViewOpen
                onClicked: sidebar.voxelViewToggleRequested()
            }
            Rectangle {
                width: parent.width
                height: Theme.borderWidth
                color: Theme.border
            }
            Repeater {
                model: sidebar.globalSections
                delegate: AppListRow {
                    required property int index
                    required property var modelData
                    iconName: modelData.iconName
                    icon: modelData.shortLabel
                    dirty: modelData.dirty
                    label: modelData.title
                    compact: true
                    selected: sidebar.section === index + 1
                    onClicked: sidebar.sectionRequested(index + 1)
                }
            }
        }
    }
    Rectangle {
        width: parent.width
        height: Theme.borderWidth
        anchors.bottom: footer.top
        color: Theme.border
    }
    Column {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.gapMedium
        spacing: Theme.gapSmall
        AppIconButton {
            readonly property bool saving: !!sidebar.session && sidebar.session.saving
            x: (parent.width - width) / 2
            iconName: sidebar.session && !saving ? "save" : ""
            iconText: saving ? "…" : ""
            label: saving ? qsTr("Saving…") : qsTr("Save world")
            dirty: !!sidebar.session && sidebar.session.dirty
            visible: !!sidebar.session
            enabled: !!sidebar.session && sidebar.session.canSave
            onClicked: sidebar.saveRequested()
        }
        AppIconButton {
            x: (parent.width - width) / 2
            iconName: "exit"
            label: qsTr("Close world")
            danger: true
            onClicked: sidebar.closeRequested()
        }
    }
}

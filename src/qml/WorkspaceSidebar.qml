import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import "controls"

// Desktop navigation rail: map, child pages, global NBT sections, save and close.
Rectangle {
    id: sidebar

    property var session: null
    property var entries: []

    signal entryRequested(var entry)
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
        ScrollBar.vertical: AppScrollBar {}
        Column {
            id: navigationColumn
            x: Theme.gapMedium
            y: Theme.gapMedium
            width: parent.width - Theme.gapMedium * 2
            spacing: Theme.gapTiny
            Repeater {
                model: sidebar.entries
                delegate: Column {
                    required property int index
                    required property var modelData

                    width: navigationColumn.width
                    spacing: Theme.gapTiny
                    Rectangle {
                        visible: modelData.beginGroup && index > 0
                        width: parent.width
                        height: Theme.borderWidth
                        color: Theme.border
                    }
                    AppListRow {
                        width: parent.width
                        iconName: modelData.iconName
                        fallbackIcon: modelData.iconText || ""
                        dirty: !!modelData.dirty
                        label: modelData.label
                        compact: true
                        selected: !!modelData.selected
                        enabled: modelData.enabled !== false
                        onClicked: sidebar.entryRequested(modelData)
                    }
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

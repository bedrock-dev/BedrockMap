import BedrockMap 1.0
import QtQuick
import "controls"

Rectangle {
    id: root
    required property Navigation navigation
    property int currentIndex: 0
    property bool darkMode: true
    property bool collapsed: false
    signal navigate(int index)
    signal closeSessionRequested(int index)
    signal toggleTheme
    signal toggleCollapsed

    color: Theme.surface
    border.color: Theme.border
    border.width: Theme.borderWidth

    Column {
        id: topSection
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: root.collapsed ? Theme.gapMedium : Theme.gapLarge
        spacing: Theme.gapMedium

        AppListRow {
            iconName: root.collapsed ? "collapse_right" : "collapse_left"
            label: qsTr("Collapse sidebar")
            compact: root.collapsed
            onClicked: root.toggleCollapsed()
        }

        AppListRow {
            label: root.navigation.pages[root.navigation.pageWorlds].title
            iconName: root.navigation.pages[root.navigation.pageWorlds].icon
            selected: currentIndex === root.navigation.pageWorlds
            compact: root.collapsed
            onClicked: navigate(root.navigation.pageWorlds)
        }

        Text {
            visible: !root.collapsed && App.openSessions.count > 0
            text: root.navigation.pages[root.navigation.pageSessions].title
            color: Theme.muted
            font.pixelSize: Theme.fontCaption
            leftPadding: Theme.gapMedium
        }
    }

    ListView {
        id: openWorldList
        visible: App.openSessions.count > 0
        anchors.top: topSection.bottom
        anchors.bottom: bottomSection.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.collapsed ? Theme.gapMedium : Theme.gapLarge
        anchors.rightMargin: root.collapsed ? Theme.gapMedium : Theme.gapLarge
        anchors.topMargin: Theme.gapSmall
        anchors.bottomMargin: Theme.gapMedium
        clip: true
        spacing: Theme.gapTiny
        model: App.openSessions
        delegate: Rectangle {
            id: sessionRow
            required property int sessionIndex
            required property string title
            required property bool dirty
            required property string kind
            readonly property bool active: App.workspaceOpen && sessionIndex === App.workspaceIndex
            width: openWorldList.width
            height: Theme.touch ? Theme.controlHeight : (root.collapsed ? Theme.sidebarCollapsedItemSize : 40)
            radius: Theme.radiusControl
            color: active ? Theme.accentSoft : ((openWorldMouse.pressed || openWorldMouse.containsMouse)
                                                ? Theme.surfaceAlt : "transparent")
            Row {
                x: root.collapsed ? (parent.width - width) / 2 : 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.gapMedium
                AppIcon {
                    width: 14
                    height: 14
                    name: kind === "mcstructure" ? "structure" : kind === "nbt" ? "nbt_editor" : "map"
                    iconSize: width
                    iconColor: active ? Theme.accent : Theme.muted
                    AppDirtyIndicator {
                        id: sessionDirtyIndicator
                        dirty: sessionRow.dirty && root.collapsed
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: -Theme.gapTiny
                    }
                }
                Text {
                    visible: !root.collapsed
                    text: (title || qsTr("Untitled world")) + (dirty ? " *" : "")
                    color: active ? Theme.accent : Theme.text
                    font.pixelSize: Theme.fontBody
                    elide: Text.ElideRight
                    width: Math.max(0, openWorldList.width - 48)
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            MouseArea {
                id: openWorldMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: App.selectWorkspace(sessionIndex)
            }
            AppIcon {
                visible: !root.collapsed
                anchors.right: parent.right
                anchors.rightMargin: Theme.gapMedium
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                name: "close"
                iconSize: width
                iconColor: (openWorldClose.pressed || openWorldClose.containsMouse) ? Theme.danger : Theme.muted
                MouseArea {
                    id: openWorldClose
                    anchors.fill: parent
                    anchors.margins: -Theme.gapSmall
                    hoverEnabled: true
                    onClicked: root.closeSessionRequested(sessionIndex)
                }
            }
            AppToolTip {
                text: title + (sessionRow.dirty ? "\n" + sessionDirtyIndicator.description : "")
                shown: root.collapsed && openWorldMouse.containsMouse
            }
        }
    }

    Column {
        id: bottomSection
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: root.collapsed ? Theme.gapMedium : Theme.gapLarge
        spacing: Theme.gapMedium

        Rectangle {
            width: parent.width
            height: Theme.borderWidth
            color: Theme.border
        }
        AppListRow {
            label: root.navigation.pages[root.navigation.pageTools].title
            iconName: root.navigation.pages[root.navigation.pageTools].icon
            selected: currentIndex === root.navigation.pageTools
            compact: root.collapsed
            onClicked: navigate(root.navigation.pageTools)
        }
        AppListRow {
            label: root.navigation.pages[root.navigation.pageSettings].title
            iconName: root.navigation.pages[root.navigation.pageSettings].icon
            selected: currentIndex === root.navigation.pageSettings
            compact: root.collapsed
            onClicked: navigate(root.navigation.pageSettings)
        }
        AppListRow {
            iconName: darkMode ? "dark" : "light"
            label: Theme.black ? qsTr("Black mode") : darkMode ? qsTr("Dark mode") : qsTr("Light mode")
            compact: root.collapsed
            onClicked: root.toggleTheme()
        }
    }
}

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
            font.pixelSize: Theme.fontSmall
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
        delegate: SessionListRow {
            width: openWorldList.width
            preferredHeight: Theme.touch ? Theme.controlHeight : (root.collapsed ? Theme.sidebarCollapsedItemSize : 40)
            railMode: root.collapsed
            showPath: false
            active: App.workspaceOpen && sessionIndex === App.workspaceIndex
            onCloseRequested: root.closeSessionRequested(sessionIndex)
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

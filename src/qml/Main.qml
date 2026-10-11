import QtQuick
import QtQuick.Window

Window {
    id: window
    visible: true
    width: 1180
    height: 760
    minimumWidth: 420
    minimumHeight: 560
    title: qsTr("BedrockMap")
    color: colors.background

    property bool mobile: width < 720
    property bool darkMode: true
    property int currentPage: 0
    property bool workspaceOpen: false
    property int workspaceIndex: 0
    property var colors: darkMode ? { background: "#111419", surface: "#191e26", surfaceAlt: "#202631", border: "#303846", text: "#f1f4f8", muted: "#929baa", accent: "#72d572", accentSoft: "#24452d", map: "#18241f" } : { background: "#f4f6f8", surface: "#ffffff", surfaceAlt: "#edf1f4", border: "#d9dfe6", text: "#1a2028", muted: "#68717e", accent: "#318b4b", accentSoft: "#dcefe0", map: "#e1eee5" }

    Rectangle { anchors.fill: parent; color: colors.background }

    ListModel { id: openSessions }
    function openWorld(path, title) {
        var found = -1
        for (var i = 0; i < openSessions.count; ++i) {
            if (openSessions.get(i).path === path) { found = i; break }
        }
        if (found < 0) {
            openSessions.append({path: path, title: title || qsTr("World workspace"), sessionIndex: openSessions.count})
            found = openSessions.count - 1
        }
        workspaceIndex = found
        workspaceOpen = true
    }
    function closeWorld(index) {
        if (index < 0 || index >= openSessions.count)
            return
        openSessions.remove(index)
        for (var i = index; i < openSessions.count; ++i)
            openSessions.setProperty(i, "sessionIndex", i)
        if (openSessions.count === 0) {
            workspaceOpen = false
            currentPage = 0
        } else {
            workspaceIndex = Math.min(index, openSessions.count - 1)
        }
    }

    AppSidebar {
        id: sidebar
        visible: !window.mobile
        width: 232
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        currentIndex: window.currentPage
        currentMapIndex: window.workspaceIndex
        sessions: openSessions
        colors: window.colors
        darkMode: window.darkMode
        onToggleTheme: window.darkMode = !window.darkMode
        onNavigate: {
            window.workspaceOpen = false
            window.currentPage = index
        }
        onMapSelected: {
            window.workspaceIndex = index
            window.workspaceOpen = true
        }
    }

    Item {
        id: content
        anchors.top: parent.top; anchors.bottom: mobileNav.top; anchors.left: sidebar.visible ? sidebar.right : parent.left; anchors.right: parent.right
        Rectangle {
            id: topbar; visible: !workspaceOpen; height: 72; anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; color: colors.background
            Text { x: 28; anchors.verticalCenter: parent.verticalCenter; text: workspaceOpen ? qsTr("World workspace") : [qsTr("Worlds"), qsTr("Maps"), qsTr("Global data"), qsTr("Tools"), qsTr("Settings")][window.currentPage]; color: colors.text; font.pixelSize: 25; font.bold: true }
            Text { x: 29; anchors.top: parent.top; anchors.topMargin: 49; text: qsTr("Bedrock Edition world workspace"); color: colors.muted; font.pixelSize: 11; visible: window.currentPage === 0 }
            Rectangle { width: 40; height: 40; radius: 20; anchors.right: parent.right; anchors.rightMargin: 22; anchors.verticalCenter: parent.verticalCenter; color: colors.surfaceAlt; Text { anchors.centerIn: parent; text: window.darkMode ? "☾" : "☀"; color: colors.text; font.pixelSize: 19 } MouseArea { anchors.fill: parent; onClicked: window.darkMode = !window.darkMode } }
        }
        Loader { anchors.top: workspaceOpen ? parent.top : topbar.bottom; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; sourceComponent: workspaceOpen ? openWorldPage : [worldsPage, mapsPage, globalDataPage, toolsPage, settingsPage][window.currentPage] }
    }

    BottomNavigation { id: mobileNav; visible: window.mobile; height: 72; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; currentIndex: window.currentPage; colors: window.colors; onNavigate: { window.workspaceOpen = false; window.currentPage = index } }

    Component { id: worldsPage; WorldListPage { colors: window.colors; onOpenWorld: window.openWorld(path, title) } }
    Component { id: mapsPage; MapsPage { colors: window.colors; sessions: openSessions; onMapSelected: { window.workspaceIndex = index; window.workspaceOpen = true } } }
    Component { id: globalDataPage; GlobalDataPage { colors: window.colors; hasActiveWorld: openSessions.count > 0 } }
    Component { id: openWorldPage; OpenWorldPage { colors: window.colors; sessions: openSessions; currentIndex: window.workspaceIndex; onCloseRequested: window.closeWorld(index); onCurrentIndexChanged: window.workspaceIndex = currentIndex } }
    Component { id: toolsPage; ToolsPage { colors: window.colors } }
    Component { id: settingsPage; SettingsPage { colors: window.colors; darkMode: window.darkMode; onToggleTheme: window.darkMode = !window.darkMode } }
}

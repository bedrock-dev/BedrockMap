import BedrockMap 1.0
import QtCore
import QtQuick
import QtQuick.Window
import QtQuick.Dialogs
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

Window {
    id: window
    readonly property Navigation navigation: Navigation {}
    visible: true
    readonly property bool touchUi: Theme.touch
    readonly property bool compactLayout: Theme.compact
    width: touchUi ? Math.max(Screen.width, 320) : 1180
    height: touchUi ? Math.max(Screen.height, 480) : 760
    minimumWidth: touchUi ? 0 : 420
    minimumHeight: touchUi ? 0 : 560
    title: qsTr("BedrockMap")
    color: Theme.background
    palette.window: Theme.background
    palette.windowText: Theme.text
    palette.base: Theme.surface
    palette.alternateBase: Theme.surfaceAlt
    palette.toolTipBase: Theme.surface
    palette.toolTipText: Theme.text
    palette.text: Theme.text
    palette.placeholderText: Theme.muted
    palette.button: Theme.surfaceAlt
    palette.buttonText: Theme.text
    palette.brightText: Theme.onAccent
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.onAccent
    palette.light: Theme.surfaceAlt
    palette.midlight: Theme.border
    palette.mid: Theme.border
    palette.dark: Theme.border
    palette.shadow: Theme.background

    property bool sidebarCollapsed: false
    readonly property real safeTop: appBackground.safeTop
    readonly property real safeBottom: appBackground.safeBottom
    readonly property real safeLeft: appBackground.safeLeft
    readonly property real safeRight: appBackground.safeRight
    // Theme chosen before the sidebar toggle overrode it; toggling back restores it.
    property string themeBeforeToggle: ""
    property string themeSetByToggle: ""
    Binding {
        target: Theme
        property: "windowWidth"
        value: window.width
    }
    Component.onCompleted: {
        sidebarCollapsed = App.runtimeSettings.collapseSidebarOnStartup
        Qt.callLater(function () {
            if (App.startupNoticeRequired)
                startupNoticeDialog.open()
            else
                App.checkForUpdates()
        })
    }
    property int currentPage: App.currentPage
    property bool workspaceOpen: App.workspaceOpen
    property int workspaceIndex: App.workspaceIndex
    property var openSessions: App.openSessions
    readonly property var activeSession: App.activeSession
    readonly property var worldWorkspace: workspaceOpen && sessionHostLazy.item ? sessionHostLazy.item.worldWorkspace :
                                                                                     null
    // Child-page stack of the visible page: the open world's workspace or the
    // tools page. The shell owns the back button and title for it.
    readonly property PageGroup childPages: {
        if (workspaceOpen)
            return worldWorkspace ? worldWorkspace.childPages : null
        if (currentPage === navigation.pageTools && toolsPageLazy.item)
            return toolsPageLazy.item.childPages
        return null
    }
    readonly property bool childPageOpen: !!childPages && childPages.isChildOpen
    // Save and close for the open session live in the compact title bar.
    readonly property bool sessionTitleBarActions: workspaceOpen && Theme.showTopbar && !!activeSession && !childPageOpen
    onChildPageOpenChanged: if (childPageOpen)
                                workspaceSectionMenu.close()
    readonly property bool canNavigateBack: childPageOpen || (!!worldWorkspace && worldWorkspace.canGoBack)
                                            || (Theme.showTopbar && workspaceOpen)
    readonly property string shellTitle: {
        if (childPageOpen)
            return childPages.currentTitle
        if (workspaceOpen)
            return activeSession ? activeSession.title + (activeSession.dirty ? " *" : "") : qsTr("Open world")
        return window.navigation.pages[currentPage].title
    }
    // Section 0 is the map; global data sections follow.
    readonly property var workspaceSectionEntries: {
        const entries = [{
                             title: qsTr("Map"),
                             iconName: "map",
                             label: qsTr("Map")
                         }]
        const sections = activeSession && activeSession.titleBarNavigationVisible ? activeSession.titleBarNavigationSections :
                                                                                   []
        for (let i = 0; i < sections.length; ++i) {
            const section = sections[i]
            entries.push({
                             title: section.title,
                             iconName: section.iconName ? section.iconName : "nbt_editor",
                             label: section.title + (section.count > 0 ? " " + section.count : "") + (section.dirty
                                                                                                       ? " *" : "")
                         })
        }
        return entries
    }
    readonly property var workspaceSectionEntry: {
        const section = activeSession && activeSession.titleBarNavigationVisible ? activeSession.titleBarNavigationIndex : 0
        return workspaceSectionEntries[section] || workspaceSectionEntries[0]
    }

    Shortcut {
        sequences: [StandardKey.Back]
        enabled: window.canNavigateBack
        onActivated: window.navigateBack()
    }
    Shortcut {
        sequence: "Ctrl+O"
        enabled: !window.workspaceOpen && window.currentPage === window.navigation.pageWorlds && !App.importingWorld
        onActivated: window.openWorldDialog()
    }
    Shortcut {
        sequence: "Ctrl+Shift+O"
        enabled: !window.workspaceOpen && window.currentPage === window.navigation.pageWorlds && !!worldsPageLazy.item
        onActivated: window.openFileDialog()
    }
    Shortcut {
        sequence: "Ctrl+N"
        onActivated: window.newNbtDocument()
    }
    Shortcut {
        sequence: "Ctrl+S"
        enabled: window.workspaceOpen && !!window.activeSession && !window.activeSession.saving
        onActivated: window.saveActiveSession()
    }
    Shortcut {
        sequence: "Ctrl+W"
        enabled: window.workspaceOpen
        onActivated: window.closeActiveSession()
    }
    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: window.quitApplication()
    }
    Shortcut {
        sequence: "Ctrl+,"
        enabled: !window.workspaceOpen
        onActivated: window.openSettingsPage()
    }

    Rectangle {
        id: appBackground
        anchors.fill: parent
        color: Theme.background
        readonly property real safeTop: SafeArea.margins.top
        readonly property real safeBottom: SafeArea.margins.bottom
        readonly property real safeLeft: SafeArea.margins.left
        readonly property real safeRight: SafeArea.margins.right
    }

    function openWorld(path, title) {
        App.openWorld(path, title)
    }
    function navigateBack() {
        // The world workspace cancels a pending region import before popping.
        if (worldWorkspace ? worldWorkspace.goBack() : childPageOpen && childPages.pop())
            return
        if (workspaceOpen)
            App.navigateToPage(window.navigation.pageSessions)
    }
    /// Worlds page: the folder picker lives here so the header button, the empty
    /// state card and a dropped folder all take the same path. App
    /// validates, reuses an open session and records the world in the recents;
    /// anything the user has to be told goes to the application-wide message
    /// stack, which is visible from every page (including an open world).
    function openWorldDialog() {
        if (!App.importingWorld)
            folderDialog.open()
    }
    function openFileDialog() {
        if (!window.workspaceOpen && window.currentPage === window.navigation.pageWorlds && worldsPageLazy.item)
            worldsPageLazy.item.openFile()
    }
    function newNbtDocument() {
        App.newNbtDocument()
    }
    function closeSession(index) {
        App.closeSession(index)
    }
    function closeActiveSession() {
        if (window.workspaceOpen)
            closeSession(window.workspaceIndex)
    }
    function quitApplication() {
        window.close()
    }
    function openSettingsPage() {
        if (!window.workspaceOpen)
            navigateToPage(window.navigation.pageSettings)
    }
    function themeIsDark(theme) {
        return theme === "black" || theme === "dark" || (theme === "system" && Theme.systemDark)
    }
    function toggleTheme() {
        const wantDark = !Theme.dark
        // A theme picked on the settings page since the last toggle replaces the remembered one.
        if (App.settings.colorTheme !== window.themeSetByToggle)
            window.themeBeforeToggle = ""
        if (window.themeBeforeToggle.length > 0 && window.themeIsDark(window.themeBeforeToggle) === wantDark) {
            App.settings.colorTheme = window.themeBeforeToggle
            window.themeBeforeToggle = ""
            window.themeSetByToggle = ""
            return
        }
        if (window.themeBeforeToggle.length === 0)
            window.themeBeforeToggle = App.settings.colorTheme
        window.themeSetByToggle = wantDark ? "dark" : "light"
        App.settings.colorTheme = window.themeSetByToggle
    }
    function navigateToPage(index) {
        App.navigateToPage(index)
    }
    function openWorldFolder(url) {
        if (!url || url.toString().length === 0)
            return
        App.openWorldFolder(url)
    }
    function saveActiveSession() {
        sessionClose.save(window.activeSession)
    }
    FolderUrlDialog {
        id: folderDialog
        title: qsTr("Open world folder")
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: window.openWorldFolder(selectedUrl)
    }

    AppMenu {
        id: workspaceSectionMenu
        parent: workspaceSectionButton
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        readonly property real sectionItemHeight: Theme.menuItemHeight
        Repeater {
            model: window.workspaceSectionEntries
            delegate: AppMenuItem {
                required property var modelData
                required property int index
                text: modelData.label
                iconName: modelData.iconName
                implicitHeight: workspaceSectionMenu.sectionItemHeight
                height: workspaceSectionMenu.sectionItemHeight
                onTriggered: if (window.activeSession && window.activeSession.titleBarNavigationVisible)
                                 window.activeSession.titleBarNavigationIndex = index
            }
        }
    }

    onClosing: function (close) {
        if (!sessionClose.requestWindowClose())
            close.accepted = false
    }
    SessionCloseFlow {
        id: sessionClose
        onWindowCloseReady: window.close()
    }
    UpdateDialog {
        id: updateDialog
    }
    StartupNoticeDialog {
        id: startupNoticeDialog
        onAccepted: App.acknowledgeStartupNotice()
        onClosed: App.checkForUpdates()
    }
    Connections {
        target: App
        function onUpdateAvailable(newVersion, releaseNotes, htmlUrl) {
            updateDialog.present(newVersion, releaseNotes, htmlUrl)
        }
    }

    ConfirmDialog {
        id: resetSettingsDialog
        title: qsTr("Reset settings")
        message: qsTr("Reset all settings to their default values? The reset will not be saved until you click Save.")
        confirmText: qsTr("Reset")
        danger: true
        onConfirmed: {
            App.settings.reset()
            App.notifications.info(qsTr("Reset to defaults, not saved yet"))
        }
    }
    ConfirmDialog {
        id: worldCopyDialog
        property url sourcePath
        title: qsTr("Copy world to internal storage")
        message: qsTr(
                     "This world cannot be edited in place with the current access permissions. Copy it to internal storage and open the copy? Changes will be saved only to the internal copy. Close Minecraft before copying.")
        confirmText: qsTr("Copy and open")
        onConfirmed: {
            App.copyWorldToInternal(sourcePath)
            sourcePath = ""
        }
        onCancelled: sourcePath = ""
    }

    // The world scan is the last failure path that had nowhere to show up: it
    // runs in the background while the page looks perfectly normal.
    Connections {
        target: App.worlds
        function onErrorChanged() {
            if (App.worlds && App.worlds.error.length > 0)
                App.notifications.error(App.worlds.error)
        }
    }
    Connections {
        target: App.settings
        function onLanguageChanged() {
            // Navigation labels come from C++ tr(). Refresh them after the
            // translator installation and engine retranslation complete.
            Qt.callLater(function () {
                window.navigation.retranslate()
            })
        }
        function onSettingsSaved() {
            App.notifications.info(
                        qsTr("Settings saved. All settings except language and font will take effect after restarting the application."))
        }
        function onErrorChanged(message) {
            App.notifications.error(message)
        }
    }
    Connections {
        target: App
        function onWorldCopyRequested(path) {
            worldCopyDialog.sourcePath = path
            worldCopyDialog.open()
        }
    }
    Connections {
        target: Qt.application
        function onStateChanged() {
            // Returning to the foreground updates permission indicators only.
            // The user refreshes worlds explicitly with the refresh button.
            if (Qt.application.state === Qt.ApplicationActive && Qt.platform.os === "android") {
                App.refreshPermissions()
            }
        }
    }

    AppSidebar {
        id: sidebar
        navigation: window.navigation
        visible: Theme.showSidebar
        width: window.sidebarCollapsed ? Theme.sidebarCollapsedWidth : Theme.sidebarWidth
        collapsed: window.sidebarCollapsed
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.leftMargin: window.safeLeft
        anchors.topMargin: window.safeTop
        anchors.bottomMargin: window.safeBottom
        currentIndex: window.workspaceOpen ? -1 : window.currentPage
        darkMode: Theme.dark
        onToggleCollapsed: window.sidebarCollapsed = !window.sidebarCollapsed
        // Preview the opposite mode; Save settings persists it.
        onToggleTheme: window.toggleTheme()
        onNavigate: index => window.navigateToPage(index)
        onCloseSessionRequested: index => window.closeSession(index)
    }

    Item {
        id: content
        anchors.top: parent.top
        anchors.bottom: mobileNav.visible ? mobileNav.top : parent.bottom
        anchors.bottomMargin: mobileNav.visible ? 0 : window.safeBottom
        anchors.left: sidebar.visible ? sidebar.right : parent.left
        anchors.right: parent.right

        // Application-wide messages, bottom-centred. Anchored to the content area
        // rather than to a page, so a failed world open is readable from the
        // worlds page, from the settings page and from inside another open world
        // alike, and because the content area already stops above the mobile
        // navigation bar the same placement works on a phone.
        NotificationStack {
            id: notices
            z: 100
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: Theme.gapSection
            width: Math.min(420, Math.max(240, parent.width - Theme.gapLarge * 2))
            center: App.notifications
        }

        Rectangle {
            id: topbar
            visible: Theme.showTopbar
            height: window.safeTop + Theme.topbarHeight
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            color: Theme.surface

            AppTitleBar {
                anchors.fill: parent
                anchors.topMargin: window.safeTop
                leftInset: window.safeLeft
                rightInset: window.safeRight
                title: window.shellTitle
                backVisible: window.canNavigateBack
                onBackRequested: window.navigateBack()

                actions: [
                    AppIconButton {
                        visible: window.currentPage === window.navigation.pageSettings && !window.workspaceOpen
                        iconName: "refresh"
                        label: qsTr("Reset settings")
                        onClicked: resetSettingsDialog.open()
                    },
                    AppIconButton {
                        visible: Theme.showTopbar && window.currentPage === window.navigation.pageSettings &&
                                 !window.workspaceOpen
                        iconName: "save"
                        label: qsTr("Save settings")
                        onClicked: App.settings.save()
                    },
                    AppButton {
                        id: workspaceSectionButton
                        visible: window.sessionTitleBarActions && window.activeSession
                                 && window.activeSession.titleBarNavigationVisible
                        compact: window.compactLayout
                        iconName: window.workspaceSectionEntry.iconName
                        text: window.workspaceSectionEntry.label
                        active: workspaceSectionMenu.opened
                        onClicked: {
                            if (workspaceSectionMenu.opened) {
                                workspaceSectionMenu.close()
                            } else {
                                workspaceSectionMenu.popup(workspaceSectionButton, workspaceSectionButton.width
                                                           - workspaceSectionMenu.width,
                                                           workspaceSectionButton.height + 4)
                            }
                        }
                    },
                    AppIconButton {
                        visible: window.sessionTitleBarActions && window.activeSession
                                 && window.activeSession.titleBarSaveVisible
                        iconName: window.activeSession && !window.activeSession.saving ? "save" : ""
                        iconText: window.activeSession && window.activeSession.saving ? "…" : ""
                        label: qsTr("Save active session")
                        dirty: !!window.activeSession && window.activeSession.dirty
                        enabled: window.activeSession && window.activeSession.titleBarSaveEnabled
                        onClicked: window.saveActiveSession()
                    },
                    AppIconButton {
                        visible: window.sessionTitleBarActions
                        iconName: "exit"
                        label: qsTr("Close active session")
                        danger: true
                        onClicked: window.closeActiveSession()
                    }
                ]
            }
        }
        StackLayout {
            id: pageStack
            anchors.top: topbar.visible ? topbar.bottom : parent.top
            anchors.topMargin: topbar.visible ? 0 : window.safeTop
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.leftMargin: sidebar.visible ? 0 : window.safeLeft
            anchors.right: parent.right
            anchors.rightMargin: window.safeRight
            currentIndex: window.workspaceOpen ? window.navigation.pageWorkspace : window.currentPage

            // Pages stay loaded after their first visit, so switching sections
            // no longer discards table modes, filters or the map scene graph.
            LazyPage {
                id: worldsPageLazy
                pageActive: !window.workspaceOpen && window.currentPage === window.navigation.pageWorlds
                sourceComponent: worldsPage
            }
            LazyPage {
                pageActive: !window.workspaceOpen && window.currentPage === window.navigation.pageSessions
                sourceComponent: openWorldsPage
            }
            LazyPage {
                id: toolsPageLazy
                pageActive: !window.workspaceOpen && window.currentPage === window.navigation.pageTools
                sourceComponent: toolsPage
            }
            LazyPage {
                pageActive: !window.workspaceOpen && window.currentPage === window.navigation.pageSettings
                sourceComponent: settingsPage
            }
            LazyPage {
                id: sessionHostLazy
                pageActive: window.workspaceOpen
                sourceComponent: sessionHostPage
            }
        }
    }

    BottomNavigation {
        id: mobileNav
        navigation: window.navigation
        visible: Theme.showBottomNavigation
        height: Theme.bottomNavigationHeight + window.safeBottom
        bottomInset: window.safeBottom
        anchors.left: parent.left
        anchors.leftMargin: window.safeLeft
        anchors.right: parent.right
        anchors.rightMargin: window.safeRight
        anchors.bottom: parent.bottom
        currentIndex: window.workspaceOpen ? -1 : window.currentPage
        onNavigate: index => window.navigateToPage(index)
    }

    AppBusyOverlay {
        anchors.fill: parent
        z: 1000
        active: App.importingWorld || App.exportingWorld
        text: App.worldTaskTitle
        detail: App.worldTaskDetail
    }

    // Open dropped world folders, world archives and standalone structure files.
    DropArea {
        id: worldDrop
        anchors.fill: parent
        onDropped: drop => {
            if (!drop.hasUrls)
                return
            for (let i = 0; i < drop.urls.length; ++i) {
                const path = drop.urls[i]
                if (path.toString().toLowerCase().endsWith(".mcworld")) {
                    App.openMcworldFile(path)
                    return
                }
                if (path.toString().toLowerCase().endsWith(".mcstructure")) {
                    App.openMcstructureFile(path)
                    return
                }
                if (App.isWorldFolder(path)) {
                    window.openWorld(path.toLocalFile(), "")
                    return
                }
            }
            App.notifications.warning(qsTr(
                                          "That drop is not a Bedrock world folder, mcworld archive or mcstructure file."))
        }
        Rectangle {
            anchors.fill: parent
            visible: worldDrop.containsDrag
            color: Theme.accent
            opacity: 0.12
            border.color: Theme.accent
            border.width: 2
        }
    }
    component LazyPage: Item {
        id: lazy
        property bool pageActive: false
        property Component sourceComponent
        property bool loadedOnce: false
        readonly property var item: lazyLoader.item
        Layout.fillWidth: true
        Layout.fillHeight: true

        Loader {
            id: lazyLoader
            anchors.fill: parent
            active: lazy.pageActive || lazy.loadedOnce
            sourceComponent: lazy.sourceComponent
            onLoaded: lazy.loadedOnce = true
        }
    }
    Component {
        id: worldsPage
        WorldListPage {
            onOpenWorld: (path, title) => window.openWorld(path, title)
            onAddRequested: window.openWorldDialog()
            onNewNbtRequested: window.newNbtDocument()
        }
    }
    Component {
        id: openWorldsPage
        OpenWorldsPage {}
    }
    Component {
        id: toolsPage
        ToolsPage {}
    }
    Component {
        id: sessionHostPage
        SessionHost {
            activeSession: window.activeSession
            currentIndex: window.workspaceIndex
            onCloseRequested: index => window.closeSession(index)
            onSaveRequested: window.saveActiveSession()
        }
    }
    Component {
        id: settingsPage
        SettingsPage {}
    }
}

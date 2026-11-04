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

    property bool sidebarCollapsed: false
    readonly property real safeTop: appBackground.safeTop
    readonly property real safeBottom: appBackground.safeBottom
    readonly property real safeLeft: appBackground.safeLeft
    readonly property real safeRight: appBackground.safeRight
    property var pendingCloseSession: null
    property var pendingShortcutSaveSession: null
    property bool closingAfterDirty: false
    onWidthChanged: Theme.windowWidth = width
    Component.onCompleted: {
        Theme.windowWidth = width
        sidebarCollapsed = App.settings.collapseSidebarOnStartup
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
    readonly property var sessionHostItem: sessionHostLazy.item
    readonly property bool structureSessionActive: !!activeSession && activeSession.kind === "mcstructure"
    readonly property bool nbtSessionActive: !!activeSession && activeSession.kind === "nbt"
    readonly property bool workspaceChildOpen: workspaceOpen && !!sessionHostItem && sessionHostItem.internalPageOpen
    readonly property bool childTitleBarVisible: compactLayout && workspaceChildOpen
    onChildTitleBarVisibleChanged: {
        if (childTitleBarVisible && workspaceSectionMenu)
            workspaceSectionMenu.close()
    }
    readonly property bool canNavigateBack: (!workspaceOpen && (currentPage === window.navigation.pageShortcuts
                                                                || currentPage === window.navigation.pagePermissions))
                                            || (compactLayout && (workspaceOpen || currentPage
                                                                  === window.navigation.pageLogs))
    readonly property string shellTitle: {
        if (workspaceOpen) {
            return activeSession ? activeSession.title + (activeSession.dirty ? " *" : "") : qsTr("Open world")
        }
        return currentPage === window.navigation.pagePermissions ? qsTr("Permissions") :
                                                                   window.navigation.pages[currentPage].title
    }
    readonly property var workspaceSections: activeSession && activeSession.kind === "world"
                                             && activeSession.globalData ? activeSession.globalData.sections : []
    readonly property int workspaceSection: activeSession && activeSession.kind === "world"
                                            ? activeSession.workspaceSection : 0
    readonly property string workspaceSectionTitle: {
        if (workspaceSection <= 0)
            return "Map"
        const index = workspaceSection - 1
        return index < workspaceSections.length ? workspaceSections[index].title : "Map"
    }
    readonly property string workspaceSectionIcon: {
        if (workspaceSection <= 0)
            return "map"
        const index = workspaceSection - 1
        if (index >= workspaceSections.length)
            return "map"
        return workspaceSections[index].iconName ? workspaceSections[index].iconName : "nbt_editor"
    }
    readonly property int workspaceSectionCount: {
        if (workspaceSection <= 0)
            return 0
        const index = workspaceSection - 1
        return index < workspaceSections.length ? workspaceSections[index].count : 0
    }
    readonly property bool workspaceSectionDirty: {
        if (workspaceSection <= 0)
            return false
        const index = workspaceSection - 1
        return index < workspaceSections.length ? workspaceSections[index].dirty : false
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
        if (workspaceOpen) {
            if (sessionHostItem && sessionHostItem.goBack())
                return
            App.navigateToPage(window.navigation.pageSessions)
            return
        }
        if (currentPage === window.navigation.pageShortcuts || currentPage === window.navigation.pageLogs
                || currentPage === window.navigation.pagePermissions)
            App.navigateToPage(window.navigation.pageTools)
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
    function navigateToPage(index) {
        App.navigateToPage(index)
    }
    function openWorldFolder(url) {
        if (!url || url.toString().length === 0)
            return
        App.openWorldFolder(url)
    }
    function saveActiveSession() {
        const session = window.activeSession
        if (!session)
            return
        if (session.kind === "nbt" && session.document && session.document.requiresSaveAs) {
            window.pendingShortcutSaveSession = session
            nbtSaveAsDialog.closeAfterSave = false
            nbtSaveAsDialog.open()
            return
        }
        if (session.commit())
            return
        if (session.kind === "world")
            App.notifications.warning(qsTr("Wait for the current map operation to finish before saving."))
        else
            App.notifications.error(qsTr("Save failed"))
    }
    function cancelPendingClose() {
        if (!window.pendingCloseSession)
            return
        window.pendingCloseSession = null
        window.closingAfterDirty = false
    }
    function finishWindowClose() {
        window.closingAfterDirty = false
        Qt.callLater(function () {
            window.close()
        })
    }
    function promptNextDirtySession() {
        if (window.pendingCloseSession)
            return
        if (!App.requestCloseFirstDirtySession()) {
            window.finishWindowClose()
            return
        }
        // A dirty session prompts synchronously. If it became clean before the
        // request was handled it is removed without a prompt, so re-check after
        // the model update instead of stopping the close sequence.
        if (!window.pendingCloseSession)
            Qt.callLater(window.promptNextDirtySession)
    }
    function completePendingSessionClose() {
        const session = window.pendingCloseSession
        if (!session)
            return
        window.pendingCloseSession = null
        App.discardSessionObject(session)
        // onClosed() continues the window-close sequence after this dialog has
        // fully closed, so the next prompt cannot race the current one.
        closeWorldDialog.close()
    }
    FolderUrlDialog {
        id: folderDialog
        title: qsTr("Open world folder")
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: window.openWorldFolder(selectedUrl)
    }
    FileUrlDialog {
        id: nbtSaveAsDialog
        property bool closeAfterSave: true
        title: qsTr("Save NBT file")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "nbt"
        nameFilters: [qsTr("NBT files (*.nbt *.nbts *.mcstructure *.palette)"), qsTr("All files (*)")]
        onAccepted: {
            const session = closeAfterSave ? window.pendingCloseSession : window.pendingShortcutSaveSession
            if (!session || !session.document)
                return
            if (!session.document.saveAs(selectedUrl)) {
                if (!closeAfterSave)
                    window.pendingShortcutSaveSession = null
                App.notifications.error(qsTr("Save failed"))
                return
            }
            if (closeAfterSave)
                window.completePendingSessionClose()
            else
                window.pendingShortcutSaveSession = null
        }
        onRejected: if (!closeAfterSave)
                        window.pendingShortcutSaveSession = null
    }

    AppMenu {
        id: workspaceSectionMenu
        readonly property real sectionItemHeight: Theme.menuItemHeight
        Repeater {
            model: {
                const items = [
                          {
                              title: "Map",
                              iconName: "map",
                              count: 0,
                              dirty: false,
                              section: 0,
                              showCount: false
                          }
                      ]
                for (let i = 0; i < window.workspaceSections.length; ++i) {
                    const section = window.workspaceSections[i]
                    items.push({
                                   title: section.title,
                                   iconName: section.iconName ? section.iconName : "nbt_editor",
                                   count: section.count,
                                   dirty: section.dirty,
                                   section: i + 1,
                                   showCount: true
                               })
                }
                return items
            }
            delegate: AppMenuItem {
                required property var modelData
                text: modelData.title + (modelData.showCount ? " " + modelData.count : "") + (modelData.dirty ? " *" :
                                                                                                                "")
                iconName: modelData.iconName
                implicitHeight: workspaceSectionMenu.sectionItemHeight
                height: workspaceSectionMenu.sectionItemHeight
                enabled: !window.childTitleBarVisible
                onTriggered: if (window.activeSession && window.activeSession.kind === "world" &&
                                     !window.childTitleBarVisible)
                                 window.activeSession.workspaceSection = modelData.section
            }
        }
    }

    Connections {
        target: App
        function onSessionCloseRequested(session) {
            window.pendingCloseSession = session
            closeWorldDialog.open()
        }
    }
    Connections {
        target: window.pendingCloseSession && window.pendingCloseSession.kind === "world" ? window.pendingCloseSession :
                                                                                            null
        function onSaved() {
            if (window.pendingCloseSession)
                window.completePendingSessionClose()
        }
        function onSaveFailed(message) {
            window.pendingCloseSession = null
            window.closingAfterDirty = false
            closeWorldDialog.close()
        }
    }
    onClosing: function (close) {
        if (window.closingAfterDirty) {
            close.accepted = false
            return
        }
        if (App.firstDirtySessionIndex() < 0)
            return
        close.accepted = false
        window.closingAfterDirty = true
        window.promptNextDirtySession()
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
    Dialog {
        id: closeWorldDialog
        modal: true
        parent: Overlay.overlay
        closePolicy: Popup.CloseOnEscape
        title: qsTr("Unsaved changes")
        width: Math.min(430, parent ? parent.width - 40 : 430)
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        padding: 18
        onClosed: {
            window.cancelPendingClose()
            if (window.closingAfterDirty)
                Qt.callLater(window.promptNextDirtySession)
        }
        background: Rectangle {
            radius: Theme.radiusCard
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
        }
        header: Item {
            implicitHeight: Theme.controlHeight + Theme.gapSmall
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.right: parent.right
                anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                text: closeWorldDialog.title
                color: Theme.text
                font.pixelSize: Theme.fontLabel
                font.bold: true
                elide: Text.ElideRight
            }
        }
        contentItem: Column {
            spacing: 14
            Text {
                width: parent.width
                visible: !!window.pendingCloseSession
                text: window.pendingCloseSession ? (window.pendingCloseSession.title || qsTr("Untitled world")) : ""
                color: Theme.text
                font.pixelSize: Theme.fontSubtitle
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: qsTr("Save changes before closing this session?")
                color: Theme.text
                font.pixelSize: Theme.fontSubtitle
                wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                AppButton {
                    text: qsTr("Cancel")
                    onClicked: closeWorldDialog.close()
                }
                AppButton {
                    danger: true
                    text: qsTr("Discard")
                    onClicked: {
                        window.completePendingSessionClose()
                    }
                }
                AppButton {
                    primary: true
                    text: window.pendingCloseSession && window.pendingCloseSession.saving ? qsTr("Saving…") : qsTr(
                                                                                                "Save")
                    enabled: window.pendingCloseSession && !window.pendingCloseSession.saving
                    onClicked: {
                        const session = window.pendingCloseSession
                        if (!session)
                            return
                        if (session.kind === "nbt") {
                            if (session.document && session.document.requiresSaveAs) {
                                nbtSaveAsDialog.closeAfterSave = true
                                nbtSaveAsDialog.open()
                                return
                            }
                            if (session.commit())
                                window.completePendingSessionClose()
                            else if (session.document)
                                App.notifications.error(qsTr("Save failed"))
                            return
                        }
                        if (!session.commit())
                            App.notifications.warning(qsTr(
                                                          "Wait for the current map operation to finish before saving."))
                    }
                }
            }
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
            window.navigation.retranslate()
        }
        function onSaved() {
            App.notifications.info(qsTr("Save successful"))
            if (App.worlds)
                App.worlds.refresh()
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
        currentIndex: window.workspaceOpen ? -1 : window.currentPage === window.navigation.pagePermissions
                                             ? window.navigation.pageTools : window.currentPage
        darkMode: Theme.dark
        onToggleCollapsed: window.sidebarCollapsed = !window.sidebarCollapsed
        // Preview the explicit opposite mode; Save settings persists it.
        onToggleTheme: App.settings.colorTheme = Theme.dark ? "light" : "dark"
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
            anchors.bottomMargin: 18
            width: Math.min(420, Math.max(240, parent.width - 24))
            center: App.notifications
        }

        Rectangle {
            id: topbar
            visible: Theme.showTopbar && !window.nbtSessionActive && !(window.workspaceOpen
                                                                       && window.structureSessionActive)
            height: window.safeTop + Theme.topbarHeight
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            color: Theme.surface
            Item {
                id: topbarContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: window.safeTop
                height: Theme.topbarHeight

                AppTitleBar {
                    visible: !window.childTitleBarVisible
                    enabled: visible
                    anchors.fill: parent
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
                            buttonSize: 34
                            onClicked: resetSettingsDialog.open()
                        },
                        AppIconButton {
                            visible: window.compactLayout && window.currentPage === window.navigation.pageSettings &&
                                     !window.workspaceOpen
                            iconName: "save"
                            label: qsTr("Save settings")
                            buttonSize: 34
                            onClicked: App.settings.save()
                        },
                        AppButton {
                            id: workspaceSectionButton
                            visible: window.workspaceOpen && window.compactLayout && window.activeSession
                                     && window.activeSession.kind === "world"
                            compact: window.compactLayout
                            iconName: window.workspaceSectionIcon
                            text: window.workspaceSectionTitle + (window.workspaceSectionCount > 0 ? " " + window.workspaceSectionCount :
                                                                                                     "") + (window.workspaceSectionDirty
                                                                                                            ? " *" : "")
                                  + "  ▾"
                            onClicked: workspaceSectionMenu.popup(workspaceSectionButton, workspaceSectionButton.width
                                                                  - workspaceSectionMenu.width,
                                                                  workspaceSectionButton.height + 4)
                        },
                        AppIconButton {
                            visible: window.workspaceOpen && window.compactLayout && window.activeSession
                                     && window.activeSession.kind === "world"
                            iconName: window.activeSession && !window.activeSession.saving ? "save" : ""
                            icon: window.activeSession && window.activeSession.saving ? "…" : ""
                            label: qsTr("Save world")
                            dirty: !!window.activeSession && window.activeSession.dirty
                            buttonSize: 34
                            enabled: !!window.activeSession && window.activeSession.kind === "world"
                                     && window.activeSession.canSave
                            onClicked: window.saveActiveSession()
                        },
                        AppIconButton {
                            visible: window.workspaceOpen && window.compactLayout && window.activeSession
                                     && window.activeSession.kind === "world"
                            iconName: "exit"
                            label: qsTr("Close world")
                            danger: true
                            buttonSize: 34
                            onClicked: window.closeActiveSession()
                        }
                    ]
                }

                AppTitleBar {
                    visible: window.childTitleBarVisible
                    enabled: visible
                    anchors.fill: parent
                    leftInset: window.safeLeft
                    rightInset: window.safeRight
                    title: window.sessionHostItem ? window.sessionHostItem.internalPageTitle : ""
                    backVisible: true
                    onBackRequested: window.navigateBack()
                }
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
                pageActive: !window.workspaceOpen && window.currentPage === window.navigation.pageTools
                sourceComponent: toolsPage
            }
            LazyPage {
                pageActive: !window.workspaceOpen && window.currentPage === window.navigation.pageShortcuts
                            && Qt.platform.os !== "android"
                sourceComponent: shortcutsPage
            }
            LazyPage {
                pageActive: !window.workspaceOpen && window.currentPage === window.navigation.pageSettings
                sourceComponent: settingsPage
            }
            LazyPage {
                pageActive: !window.workspaceOpen && window.currentPage === window.navigation.pageLogs
                sourceComponent: logsPage
            }
            LazyPage {
                id: sessionHostLazy
                pageActive: window.workspaceOpen
                sourceComponent: sessionHostPage
            }
            LazyPage {
                pageActive: !window.workspaceOpen && window.currentPage === window.navigation.pagePermissions
                sourceComponent: permissionsPage
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
        currentIndex: window.workspaceOpen ? -1 : window.currentPage === window.navigation.pagePermissions
                                             ? window.navigation.pageTools : window.currentPage
        onNavigate: index => window.navigateToPage(index)
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
        ToolsPage {
            navigation: window.navigation
        }
    }
    Component {
        id: shortcutsPage
        ShortcutsPage {}
    }
    Component {
        id: sessionHostPage
        SessionHost {
            activeSession: window.activeSession
            currentIndex: window.workspaceIndex
            onCloseRequested: index => window.closeSession(index)
            onBackRequested: window.navigateBack()
            onSaveRequested: window.saveActiveSession()
        }
    }
    Component {
        id: settingsPage
        SettingsPage {}
    }
    Component {
        id: logsPage
        LogPage {}
    }
    Component {
        id: permissionsPage
        PermissionsPage {}
    }
}

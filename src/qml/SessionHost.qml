import BedrockMap 1.0
pragma ComponentBehavior: Bound

import QtQuick

/// Retains each kind of workspace page while switching between sessions.
Item {
    id: host
    property var activeSession
    property int currentIndex: 0
    property var worldSession: null
    property var nbtSession: null
    property var structureSession: null
    readonly property bool structureActive: !!activeSession && activeSession.kind === "mcstructure"
    property bool structureLoaded: false
    property bool worldLoaded: false
    property bool nbtLoaded: false
    readonly property bool nbtActive: !!activeSession && activeSession.kind === "nbt"
    readonly property bool worldVisible: !!worldSession && !!activeSession && activeSession.kind === "world"
    readonly property bool nbtVisible: !!nbtSession && nbtActive
    readonly property var worldPage: worldLoader.item
    readonly property bool internalPageOpen: worldVisible && worldPage ? worldPage.internalPageOpen === true : false
    readonly property string internalPageTitle: internalPageOpen ? worldPage.internalPageTitle : ""
    signal closeRequested(int index)

    Connections {
        target: App
        function onSessionAboutToClose(session) {
            if (host.worldSession === session) {
                host.worldLoaded = false
                host.worldSession = null
            }
            if (host.nbtSession === session) {
                host.nbtLoaded = false
                host.nbtSession = null
            }
            if (host.structureSession === session) {
                host.structureLoaded = false
                host.structureSession = null
            }
        }
    }

    function syncSession() {
        if (!activeSession)
            return
        if (activeSession.kind === "nbt")
            nbtSession = activeSession
        else if (activeSession.kind === "mcstructure")
            structureSession = activeSession
        else if (activeSession.kind === "world")
            worldSession = activeSession
    }

    onActiveSessionChanged: syncSession()
    Component.onCompleted: syncSession()

    function goBack() {
        return worldVisible && worldPage ? worldPage.goBack() : false
    }

    Loader {
        id: worldLoader
        anchors.fill: parent
        active: !!host.worldSession && (host.worldVisible || host.worldLoaded)
        visible: host.worldVisible
        sourceComponent: worldComponent
        onLoaded: host.worldLoaded = true
    }

    Loader {
        id: nbtLoader
        anchors.fill: parent
        active: !!host.nbtSession && (host.nbtActive || host.nbtLoaded)
        visible: host.nbtVisible
        sourceComponent: nbtComponent
        onLoaded: host.nbtLoaded = true
    }

    Loader {
        anchors.fill: parent
        active: !!host.structureSession && (host.structureActive || host.structureLoaded)
        visible: host.structureActive
        sourceComponent: structureComponent
        onLoaded: host.structureLoaded = true
    }

    Component {
        id: structureComponent
        McstructurePage {
            session: host.structureSession
            onCloseRequested: host.closeRequested(host.currentIndex)
        }
    }

    Component {
        id: worldComponent
        OpenWorldPage {
            currentIndex: host.currentIndex
            activeSession: host.worldSession
            onCloseRequested: index => host.closeRequested(index)
        }
    }

    Component {
        id: nbtComponent
        NbtEditorPage {
            document: host.nbtSession ? host.nbtSession.document : null
            onCloseRequested: host.closeRequested(host.currentIndex)
        }
    }
}

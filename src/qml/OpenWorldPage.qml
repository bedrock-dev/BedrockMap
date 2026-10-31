import BedrockMap 1.0
import QtQuick

Item {
    id: page
    property var activeSession
    property int currentIndex: 0
    readonly property var worldWorkspace: activeSession && activeSession.ready ? loader.item : null
    readonly property bool internalPageOpen: !!worldWorkspace && worldWorkspace.childPageOpen === true
    readonly property string internalPageTitle: internalPageOpen ? worldWorkspace.childPageTitle : ""
    signal closeRequested(int index)

    function goBack() {
        return internalPageOpen && worldWorkspace.goBack()
    }

    Loader {
        id: loader
        anchors.fill: parent
        sourceComponent: page.activeSession && page.activeSession.ready ? workspaceComponent : loadingComponent
    }

    Component {
        id: loadingComponent
        Item {
            property var session: page.activeSession
            Rectangle {
                anchors.fill: parent
                color: Theme.background
            }
            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 96, 520)
                spacing: 16
                Text {
                    width: parent.width
                    text: session && session.indexLoading ? qsTr("Building world coordinate index") : qsTr(
                                                                "Opening world")
                    color: Theme.text
                    font.pixelSize: Theme.fontHeading
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    width: parent.width
                    text: session && session.indexLoading ? qsTr(
                                                                "The map will be available when indexing is complete.") :
                                                            qsTr("Opening LevelDB and preparing the world...")
                    color: Theme.muted
                    font.pixelSize: Theme.fontSubtitle
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
                Item {
                    width: parent.width
                    height: 10
                    visible: !!session
                    Rectangle {
                        anchors.fill: parent
                        radius: height / 2
                        color: Theme.surfaceAlt
                        border.color: Theme.border
                    }
                    Rectangle {
                        id: progressFill
                        width: parent.width * 0.28
                        height: parent.height
                        radius: height / 2
                        color: Theme.accent
                        opacity: 0.9
                        SequentialAnimation on x {
                            running: progressFill.visible
                            loops: Animation.Infinite
                            NumberAnimation {
                                from: 0
                                to: progressFill.parent.width - progressFill.width
                                duration: 1200
                                easing.type: Easing.InOutQuad
                            }
                            PauseAnimation {
                                duration: 120
                            }
                            NumberAnimation {
                                from: progressFill.parent.width - progressFill.width
                                to: 0
                                duration: 1200
                                easing.type: Easing.InOutQuad
                            }
                            PauseAnimation {
                                duration: 120
                            }
                        }
                    }
                }
                Text {
                    width: parent.width
                    text: session && session.indexLoading ? qsTr("Scanned keys: %1   Chunks: %2").arg(
                                                                session.scannedKeys).arg(session.indexedChunks) : qsTr(
                                                                "Please wait...")
                    color: Theme.muted
                    font.pixelSize: Theme.fontBody
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }

    Component {
        id: workspaceComponent
        WorldWorkspace {
            session: page.activeSession
            onCloseRequested: page.closeRequested(page.currentIndex)
        }
    }
}

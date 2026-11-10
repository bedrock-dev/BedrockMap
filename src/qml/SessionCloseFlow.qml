import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import "controls"

// Saving sessions, the unsaved-changes prompt for closing a session, and the
// window close sequence that prompts for every dirty session in turn.
Item {
    id: root
    // Session shown in the unsaved-changes prompt.
    property var closingSession: null
    // Session waiting for the Save As dialog. It closes after saving when it is
    // also closingSession.
    property var saveAsSession: null
    // Set while closing the window walks through the dirty sessions.
    property bool closingWindow: false
    signal windowCloseReady

    function save(session) {
        if (!session)
            return
        if (session.document && session.document.requiresSaveAs) {
            openSaveAs(session)
            return
        }
        if (!session.commit())
            App.notifications.error(qsTr("Save failed"))
    }

    /// Returns true when the window may close now. Otherwise prompts for each
    /// dirty session and emits windowCloseReady() once all are resolved.
    function requestWindowClose() {
        if (closingWindow)
            return false
        if (App.firstDirtySessionIndex() < 0)
            return true
        closingWindow = true
        promptNextDirtySession()
        return false
    }

    function openSaveAs(session) {
        saveAsSession = session
        const filename = session.document.suggestedSaveFileName()
        saveAsDialog.selectedFile = saveAsDialog.currentFolder.toString().length > 0
                ? saveAsDialog.currentFolder + "/" + encodeURIComponent(filename) : filename
        saveAsDialog.open()
    }

    function promptNextDirtySession() {
        if (closingSession)
            return
        if (!App.requestCloseFirstDirtySession()) {
            closingWindow = false
            Qt.callLater(root.windowCloseReady)
            return
        }
        // A dirty session prompts synchronously. If it became clean before the
        // request was handled it is removed without a prompt, so re-check after
        // the model update instead of stopping the close sequence.
        if (!closingSession)
            Qt.callLater(promptNextDirtySession)
    }

    function completeSessionClose() {
        const session = closingSession
        if (!session)
            return
        closingSession = null
        App.discardSessionObject(session)
        // onClosed() continues the window close sequence after the prompt has
        // fully closed, so the next prompt cannot race the current one.
        closePrompt.close()
    }

    function abortClose() {
        closingSession = null
        closingWindow = false
        closePrompt.close()
    }

    Connections {
        target: App
        function onSessionCloseRequested(session) {
            root.closingSession = session
            closePrompt.open()
        }
    }
    Connections {
        target: root.closingSession && root.closingSession.saveCompletesAsynchronously ? root.closingSession : null
        function onSaved() {
            root.completeSessionClose()
        }
        function onSaveFailed(message) {
            root.abortClose()
        }
    }

    FileUrlDialog {
        id: saveAsDialog
        title: qsTr("Save NBT file")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "nbt"
        nameFilters: [qsTr("NBT files (*.nbt *.nbts *.mcstructure *.palette)"), qsTr("All files (*)")]
        onAccepted: {
            const session = root.saveAsSession
            root.saveAsSession = null
            if (!session || !session.document)
                return
            if (!session.document.saveAs(selectedUrl)) {
                App.notifications.error(qsTr("Save failed"))
                return
            }
            if (session === root.closingSession)
                root.completeSessionClose()
        }
        onRejected: root.saveAsSession = null
    }

    AppDialog {
        id: closePrompt
        title: qsTr("Unsaved changes")
        onClosed: {
            // The session is still set when the prompt was dismissed instead of resolved.
            if (root.closingSession) {
                root.closingSession = null
                root.closingWindow = false
                return
            }
            if (root.closingWindow)
                Qt.callLater(root.promptNextDirtySession)
        }
        contentItem: Column {
            spacing: Theme.gapMedium
            Text {
                width: parent.width
                visible: !!root.closingSession
                text: root.closingSession ? (root.closingSession.title || qsTr("Untitled world")) : ""
                color: Theme.text
                font.pixelSize: Theme.fontSubtitle
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
                spacing: Theme.gapMedium
                AppButton {
                    text: qsTr("Cancel")
                    onClicked: closePrompt.close()
                }
                AppButton {
                    danger: true
                    text: qsTr("Discard")
                    onClicked: root.completeSessionClose()
                }
                AppButton {
                    primary: true
                    text: root.closingSession && root.closingSession.saving ? qsTr("Saving…") : qsTr("Save")
                    enabled: !!root.closingSession && !root.closingSession.saving
                    onClicked: {
                        const session = root.closingSession
                        if (!session)
                            return
                        if (session.document && session.document.requiresSaveAs) {
                            root.openSaveAs(session)
                            return
                        }
                        if (!session.commit()) {
                            App.notifications.error(qsTr("Save failed"))
                            return
                        }
                        if (!session.saveCompletesAsynchronously)
                            root.completeSessionClose()
                    }
                }
            }
        }
    }
}

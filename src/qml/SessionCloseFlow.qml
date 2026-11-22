import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import "controls"

// Centralizes confirmed saves, the unsaved-changes prompt for closing a session,
// and the window close sequence that prompts for every dirty session in turn.
Item {
    id: root
    // Session and intent waiting for the save confirmation dialog.
    property var pendingSaveSession: null
    property string pendingSaveIntent: "manual"
    // Session shown in the unsaved-changes prompt.
    property var closingSession: null
    // Session waiting for the Save As dialog. It closes after saving when it is
    // also closingSession.
    property var saveAsSession: null
    property string saveAsIntent: "manual"
    // Prevents a normal asynchronous save from completing an unrelated close.
    property bool closeSaveInProgress: false
    // Set while closing the window walks through the dirty sessions.
    property bool closingWindow: false
    signal windowCloseReady

    function intentRequiresClose(intent) {
        return intent === "closeSession" || intent === "closeWindow"
    }

    function save(session, intent) {
        if (!session)
            return false
        if (session.saving || saveConfirmation.opened)
            return false
        pendingSaveSession = session
        pendingSaveIntent = intent || "manual"
        saveConfirmation.open()
        return true
    }

    function cancelPendingSave() {
        pendingSaveSession = null
        pendingSaveIntent = "manual"
        saveConfirmation.close()
    }

    function confirmPendingSave() {
        const session = pendingSaveSession
        const intent = pendingSaveIntent
        pendingSaveSession = null
        pendingSaveIntent = "manual"
        saveConfirmation.close()
        if (session)
            beginSave(session, intent)
    }

    function beginSave(session, intent) {
        if (!session || session.saving)
            return
        if (session.document && session.document.requiresSaveAs) {
            openSaveAs(session, intent)
            return
        }
        closeSaveInProgress = intentRequiresClose(intent)
        if (!session.commit()) {
            closeSaveInProgress = false
            App.notifications.error(qsTr("Save failed"))
            return
        }
        // Synchronous saves and no-op commits do not emit saved(), so close the
        // originating prompt here. Asynchronous world saves remain open until
        // saved() or saveFailed() arrives.
        if (intentRequiresClose(intent) && session === closingSession && !session.saving)
            completeSessionClose()
    }

    /// Returns true when the window may close now. Otherwise prompts for each
    /// dirty session and emits windowCloseReady() once all are resolved.
    function requestWindowClose() {
        if (closingWindow || saveConfirmation.opened || pendingSaveSession)
            return false
        if (App.firstDirtySessionIndex() < 0)
            return true
        closingWindow = true
        promptNextDirtySession()
        return false
    }

    function selectedSaveFile(dialog, fileName) {
        const folder = dialog.currentFolder.toString()
        if (folder.length === 0)
            return fileName
        return folder + "/" + encodeURIComponent(fileName)
    }

    function openSaveAs(session, intent) {
        saveAsSession = session
        saveAsIntent = intent || "manual"
        const filename = session.document.suggestedSaveFileName()
        saveAsDialog.selectedFile = selectedSaveFile(saveAsDialog, filename)
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
        closeSaveInProgress = false
        closingSession = null
        App.discardSessionObject(session)
        // onClosed() continues the window close sequence after the prompt has
        // fully closed, so the next prompt cannot race the current one.
        closePrompt.close()
    }

    function abortClose() {
        closeSaveInProgress = false
        closingSession = null
        closingWindow = false
        closePrompt.close()
    }

    Connections {
        target: App
        function onSessionCloseRequested(session) {
            root.closeSaveInProgress = false
            root.closingSession = session
            closePrompt.open()
        }
    }
    Connections {
        target: root.closingSession && root.closeSaveInProgress && root.closingSession.saveCompletesAsynchronously
                ? root.closingSession : null
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
            const intent = root.saveAsIntent
            root.saveAsSession = null
            root.saveAsIntent = "manual"
            if (!session || !session.document)
                return
            if (!session.document.saveAs(selectedUrl)) {
                App.notifications.error(qsTr("Save failed"))
                return
            }
            if (root.intentRequiresClose(intent) && session === root.closingSession)
                root.completeSessionClose()
        }
        onRejected: {
            root.saveAsSession = null
            root.saveAsIntent = "manual"
        }
    }

    AppDialog {
        id: saveConfirmation
        title: qsTr("Confirm save")
        onClosed: {
            if (root.pendingSaveSession) {
                root.pendingSaveSession = null
                root.pendingSaveIntent = "manual"
            }
        }
        contentItem: Column {
            spacing: Theme.gapMedium
            Text {
                width: parent.width
                visible: !!root.pendingSaveSession
                text: root.pendingSaveSession ? (root.pendingSaveSession.title || qsTr("Untitled world")) : ""
                color: Theme.text
                font.pixelSize: Theme.fontMedium
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: qsTr("Save the pending in-memory changes?")
                color: Theme.text
                font.pixelSize: Theme.fontMedium
                wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.gapMedium
                AppButton {
                    text: qsTr("Cancel")
                    onClicked: root.cancelPendingSave()
                }
                AppButton {
                    text: qsTr("Save")
                    primary: true
                    enabled: !!root.pendingSaveSession && !root.pendingSaveSession.saving
                    onClicked: root.confirmPendingSave()
                }
            }
        }
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
                font.pixelSize: Theme.fontMedium
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: qsTr("Save changes before closing this session?")
                color: Theme.text
                font.pixelSize: Theme.fontMedium
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
                    onClicked: root.save(root.closingSession, root.closingWindow ? "closeWindow" : "closeSession")
                }
            }
        }
    }
}

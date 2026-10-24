import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "controls"

Control {
    id: page
    font.pixelSize: Theme.fontBody
    palette.window: Theme.surface
    palette.base: Theme.surface
    palette.text: Theme.text
    palette.windowText: Theme.text
    palette.button: Theme.surfaceAlt
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.onAccent
    palette.placeholderText: Theme.muted

    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export logs")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "log"
        nameFilters: [qsTr("Log files (*.log *.txt)")]
        onAccepted: {
            if (!Logs.save(selectedFile))
                Notifications.error(Logs.error)
        }
    }
    AppMenu {
        id: copyMenu
        property string selection: ""
        AppMenuItem {
            text: qsTr("Copy")
            enabled: copyMenu.selection.length > 0
            onTriggered: Logs.copyText(copyMenu.selection)
        }
        AppMenuItem {
            text: qsTr("Copy displayed logs")
            onTriggered: Logs.copyAll()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                AppTextField {
                    id: search
                    Layout.fillWidth: true
                    Layout.minimumWidth: 60
                    placeholderText: qsTr("Search logs")
                    text: Logs.filter
                    onTextEdited: searchDelay.restart()
                    Timer {
                        id: searchDelay
                        interval: 200
                        onTriggered: Logs.filter = search.text
                    }
                }
                AppComboBox {
                    Layout.preferredWidth: 130
                    model: [qsTr("All levels"), qsTr("Info and above"), qsTr("Warnings and above"), qsTr("Errors")]
                    currentIndex: Logs.minimumLevel
                    onActivated: Logs.minimumLevel = currentIndex
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                AppCheckBox {
                    id: follow
                    text: qsTr("Auto-scroll")
                    checked: true
                    onToggled: if (checked)
                                   logView.scrollToEnd()
                }
                Item {
                    Layout.fillWidth: true
                }
                AppButton {
                    text: qsTr("Copy")
                    enabled: Logs.count > 0
                    onClicked: Logs.copyAll()
                    tooltip: qsTr("Copy displayed logs")
                }
                AppButton {
                    text: qsTr("Export")
                    enabled: Logs.logFile.length > 0 || Logs.storedCount > 0
                    onClicked: exportDialog.open()
                    tooltip: qsTr("Export the complete log file")
                }
                AppButton {
                    text: qsTr("Clear")
                    enabled: Logs.storedCount > 0
                    onClicked: Logs.clear()
                    tooltip: qsTr("Clear the view; keep the log file")
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Flickable {
                id: logView
                anchors.fill: parent
                clip: true
                contentWidth: width
                contentHeight: message.height + 20
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                onMovementStarted: follow.checked = false
                Component.onCompleted: scrollToEnd()

                function scrollToEnd() {
                    Qt.callLater(function () {
                        contentY = Math.max(0, contentHeight - height)
                    })
                }

                TextEdit {
                    id: message
                    x: 12
                    y: 10
                    width: logView.width - 24
                    height: implicitHeight
                    text: Logs.displayHtml
                    textFormat: TextEdit.RichText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    font.family: "JetBrains Mono"
                    font.pixelSize: 11
                    color: Theme.text
                    selectedTextColor: Theme.onAccent
                    selectionColor: Theme.accent
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: {
                            copyMenu.selection = message.selectedText
                            copyMenu.popup()
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onLongPressed: {
                            copyMenu.selection = message.selectedText
                            copyMenu.popup()
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: Logs.count === 0
                text: Logs.storedCount > 0 ? qsTr("No matching logs") : qsTr("No logs")
                color: Theme.muted
                font.pixelSize: Theme.fontBody
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            spacing: 4
            Label {
                Layout.fillWidth: true
                text: qsTr("%1 displayed / %2 retained | %3 older entries omitted").arg(Logs.count).arg(
                          Logs.storedCount).arg(Logs.omittedCount)
                color: Theme.muted
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: Logs.logFile
                color: Theme.muted
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideMiddle
                AppToolTip {
                    text: Logs.logFile
                    shown: pathHover.hovered
                }
                HoverHandler {
                    id: pathHover
                }
            }
        }
    }
    Connections {
        target: Logs
        function onCountChanged() {
            if (follow.checked)
                logView.scrollToEnd()
        }
    }
}

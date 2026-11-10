import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

Item {
    id: page
    onVisibleChanged: if (visible)
                          App.refreshPermissions()
    Component.onCompleted: App.refreshPermissions()

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: Theme.gapSection
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroll.availableWidth
            spacing: Theme.gapMedium

            PermissionEntry {
                title: qsTr("All files access")
                detail: qsTr("Allow access to shared storage. Tap the button to request access in Android.")
                status: App.allFilesPermission ? qsTr("Granted") : qsTr("Not granted")
                granted: App.allFilesPermission
                requestEnabled: !App.allFilesPermission
                onRequestAccess: App.requestAllFilesPermission()
            }
            PermissionEntry {
                title: qsTr("Shizuku service")
                detail: {
                    switch (App.shizukuPermission) {
                    case "Unavailable":
                        return qsTr("Install and start Shizuku, then return here to request access.")
                    case "Unsupported":
                        return qsTr("Update Shizuku to version 11 or later.")
                    case "Denied":
                        return qsTr(
                                    "Access was permanently denied. Allow this app in Shizuku's authorized applications.")
                    default:
                        return qsTr("Tap the button to request Shizuku access.")
                    }
                }
                status: {
                    switch (App.shizukuPermission) {
                    case "Granted":
                        return qsTr("Granted")
                    case "NotGranted":
                        return qsTr("Not granted")
                    case "Requesting":
                        return qsTr("Waiting for authorization…")
                    case "Denied":
                        return qsTr("Denied")
                    case "Unsupported":
                        return qsTr("Unsupported version")
                    case "Error":
                        return qsTr("Request failed")
                    default:
                        return qsTr("Service unavailable")
                    }
                }
                granted: App.shizukuPermission === "Granted"
                requestEnabled: App.shizukuPermission === "NotGranted" || App.shizukuPermission === "Error"
                onRequestAccess: App.requestShizukuPermission()
            }
            PermissionEntry {
                title: qsTr("Root access")
                detail: qsTr(
                            "Tap the button to request access through your root manager. The authorized connection is reused for world scanning.")
                status: {
                    switch (App.rootPermission) {
                    case "Granted":
                        return qsTr("Granted")
                    case "Requesting":
                        return qsTr("Waiting for authorization…")
                    case "Denied":
                        return qsTr("Denied")
                    case "Unavailable":
                        return qsTr("Root unavailable")
                    case "Timeout":
                        return qsTr("Request timed out")
                    case "Error":
                        return qsTr("Request failed")
                    default:
                        return qsTr("Not checked")
                    }
                }
                granted: App.rootPermission === "Granted"
                requestEnabled: App.rootPermission !== "Requesting"
                actionText: App.rootPermission === "Granted" ? qsTr("Check again") : qsTr("Request access")
                onRequestAccess: App.requestRootPermission()
            }
            AppCard {
                Layout.fillWidth: true
                implicitHeight: permissionHelp.implicitHeight + Theme.gapLarge * 2

                ColumnLayout {
                    id: permissionHelp
                    x: Theme.gapLarge
                    y: Theme.gapLarge
                    width: parent.width - Theme.gapLarge * 2
                    spacing: Theme.gapMedium

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("What are these permissions for?")
                        color: Theme.text
                        font.pixelSize: Theme.fontLabel
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        model: [
                            qsTr("All files access: lets the app scan, read and save worlds in shared storage."),
                            qsTr("Shizuku: provides access to additional world directories through the Shizuku service, depending on Android restrictions and how the service is running."),
                            qsTr("Root: lets the app read and save worlds in protected directories on rooted devices."),
                            qsTr("All of these permissions are optional. You can use the app normally without granting them: import worlds or files for editing, then export the results.")
                        ]
                        delegate: Label {
                            required property string modelData
                            Layout.fillWidth: true
                            text: modelData
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }

    component PermissionEntry: AppCard {
        id: entry
        property string title
        property string detail
        property string status
        property string actionText: qsTr("Request access")
        property bool granted: false
        property bool requestEnabled: false
        signal requestAccess
        Layout.fillWidth: true
        implicitHeight: body.implicitHeight + Theme.gapLarge * 2

        ColumnLayout {
            id: body
            x: Theme.gapLarge
            y: Theme.gapLarge
            width: parent.width - Theme.gapLarge * 2
            spacing: Theme.gapSmall

            Label {
                Layout.fillWidth: true
                text: entry.title
                color: Theme.text
                font.pixelSize: Theme.fontLabel
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                visible: entry.detail.length > 0
                text: entry.detail
                color: Theme.muted
                font.pixelSize: Theme.fontBody
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: entry.status
                    color: entry.granted ? Theme.accent : Theme.muted
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.Wrap
                }
                AppButton {
                    text: entry.actionText
                    enabled: entry.requestEnabled
                    onClicked: entry.requestAccess()
                }
            }
        }
    }
}

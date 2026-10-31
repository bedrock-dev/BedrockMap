import BedrockMap 1.0
import QtQuick
import "controls"

Item {
    id: page
    required property Navigation navigation
    Flickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.gapSection
        clip: true
        Column {
            id: column
            x: Theme.pageGutter
            y: Theme.gapMedium
            width: parent.width - Theme.pageGutter * 2
            spacing: Theme.gapLarge
            AppListRow {
                visible: Qt.platform.os === "android"
                label: qsTr("Permissions")
                detail: qsTr("Manage storage access and privileged services")
                iconName: "settings"
                onClicked: App.navigateToPage(page.navigation.pagePermissions)
            }
            AppListRow {
                label: qsTr("Show logs")
                detail: qsTr("Inspect application events and diagnostics")
                iconName: "log"
                onClicked: App.navigateToPage(page.navigation.pageLogs)
            }
            AppListRow {
                label: qsTr("Open config folder")
                detail: qsTr("Open config.ini and the application data directory")
                iconName: "settings"
                onClicked: App.openConfigDirectory()
            }
        }
    }
}

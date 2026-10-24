import BedrockMap 1.0
import QtQuick
import "controls"

Item {
    id: page
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
                label: qsTr("Show logs")
                detail: qsTr("Inspect application events and diagnostics")
                iconName: "log"
                onClicked: App.navigateToPage(Navigation.pageLogs)
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

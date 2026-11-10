import BedrockMap 1.0
import QtQuick
import QtQuick.Layouts
import "controls"

Item {
    id: page

    readonly property alias childPages: pageGroup

    onVisibleChanged: if (!visible)
                          pageGroup.reset("root")

    Flickable {
        anchors.fill: parent
        visible: pageGroup.isRoot
        contentHeight: column.height + Theme.gapSection
        clip: true
        Column {
            id: column
            x: Theme.gapSection
            y: Theme.gapMedium
            width: parent.width - Theme.gapSection * 2
            spacing: Theme.gapLarge
            AppListRow {
                visible: Qt.platform.os === "android"
                label: qsTr("Permissions")
                detail: qsTr("Manage storage access and privileged services")
                iconName: "settings"
                onClicked: pageGroup.push("permissions")
            }
            AppListRow {
                visible: !Theme.touch
                label: qsTr("Keyboard shortcuts")
                detail: qsTr("View shortcuts for the application, map and 3D view")
                iconName: "shortcuts"
                onClicked: pageGroup.push("shortcuts")
            }
            AppListRow {
                label: qsTr("Show logs")
                detail: qsTr("Inspect application events and diagnostics")
                iconName: "log"
                onClicked: pageGroup.push("logs")
            }
            AppListRow {
                label: qsTr("Open config folder")
                detail: qsTr("Open config.ini and the application data directory")
                iconName: "settings"
                onClicked: App.openConfigDirectory()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: pageGroup.isChildOpen
        spacing: 0

        AppTitleBar {
            visible: !Theme.showTopbar
            embedded: true
            title: pageGroup.currentTitle
            backVisible: true
            showBottomBorder: false
            leftInset: Theme.gapSection
            rightInset: Theme.gapSection
            Layout.fillWidth: true
            Layout.topMargin: Theme.gapMedium
            onBackRequested: pageGroup.pop()
        }
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: pageGroup.currentComponent
        }
    }

    Component {
        id: permissionsComponent
        PermissionsPage {}
    }
    Component {
        id: shortcutsComponent
        ShortcutsPage {}
    }
    Component {
        id: logsComponent
        LogPage {}
    }
    PageGroup {
        id: pageGroup
        rootRoute: "root"
        definitions: [
            {
                route: "root",
                title: qsTr("Tools")
            },
            {
                route: "permissions",
                title: qsTr("Permissions"),
                component: permissionsComponent
            },
            {
                route: "shortcuts",
                title: qsTr("Keyboard shortcuts"),
                component: shortcutsComponent
            },
            {
                route: "logs",
                title: qsTr("Logs"),
                component: logsComponent
            }
        ]
    }
}

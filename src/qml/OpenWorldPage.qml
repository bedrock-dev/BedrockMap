import QtQuick

Item {
    id: page
    property var colors
    property var sessions
    property int currentIndex: 0
    signal closeRequested(int index)

    Loader { id: loader; anchors.fill: parent; sourceComponent: workspaceComponent }

    Component { id: workspaceComponent; WorldWorkspace { colors: page.colors; worldTitle: page.sessions.count > 0 ? page.sessions.get(page.currentIndex).title : qsTr("World workspace"); worldPath: page.sessions.count > 0 ? page.sessions.get(page.currentIndex).path : ""; onCloseRequested: page.closeRequested(page.currentIndex) } }
}

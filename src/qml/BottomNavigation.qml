import BedrockMap 1.0
import QtQuick
import "controls"

Rectangle {
    id: root
    property int currentIndex: 0
    property real bottomInset: 0
    signal navigate(int index)
    color: Theme.surface
    border.color: Theme.border
    border.width: 1
    Row {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.topMargin: 6
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 6 + root.bottomInset
        Repeater {
            model: Navigation.bottomPages
            delegate: Item {
                required property var modelData
                width: parent.width / Navigation.bottomPages.length
                height: parent.height
                AppNavigationButton {
                    iconName: modelData.icon
                    label: modelData.shortTitle
                    selected: modelData.page === currentIndex
                    onClicked: navigate(modelData.page)
                }
            }
        }
    }
}

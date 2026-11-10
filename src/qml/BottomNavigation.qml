import BedrockMap 1.0
import QtQuick
import "controls"

Rectangle {
    id: root
    required property Navigation navigation
    property int currentIndex: 0
    property real bottomInset: 0
    signal navigate(int index)
    color: Theme.surface
    border.color: Theme.border
    border.width: Theme.borderWidth
    Row {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.gapMedium
        anchors.rightMargin: Theme.gapMedium
        anchors.topMargin: Theme.gapMedium
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.gapMedium + root.bottomInset
        Repeater {
            model: root.navigation.pages
            delegate: Item {
                required property var modelData
                width: parent.width / root.navigation.pages.length
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

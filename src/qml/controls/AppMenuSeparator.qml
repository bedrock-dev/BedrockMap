import BedrockMap 1.0
import QtQuick
import QtQuick.Controls

MenuSeparator {
    id: root

    implicitHeight: visible ? Theme.ui(9) : 0
    height: implicitHeight
    leftPadding: 0
    topPadding: 0
    rightPadding: 0
    bottomPadding: 0

    contentItem: Item {
        implicitHeight: 1
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 1
            color: Theme.border
        }
    }
}

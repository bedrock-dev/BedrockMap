import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Window

MenuSeparator {
    id: root

    implicitHeight: visible ? Theme.gapMedium : 0
    height: implicitHeight
    leftPadding: 0
    topPadding: 0
    rightPadding: 0
    bottomPadding: 0

    contentItem: Item {
        implicitHeight: 1
        Rectangle {
            readonly property real pixelRatio: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
            readonly property real lineHeight: 1 / pixelRatio

            width: parent.width
            height: lineHeight
            y: Math.round((parent.height - lineHeight) * pixelRatio / 2) / pixelRatio
            antialiasing: false
            color: Theme.border
        }
    }
}

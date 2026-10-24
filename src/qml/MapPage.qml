import BedrockMap 1.0
import QtQuick
import "controls"

Item {
    id: page
    Rectangle {
        anchors.fill: parent
        anchors.margins: 12
        radius: Theme.radiusPill
        color: Theme.map
        border.color: Theme.border
        Repeater {
            model: 24
            delegate: Rectangle {
                required property int index
                width: 1
                height: parent.height
                x: index * (parent.width / 24)
                color: Theme.border
                opacity: 0.28
            }
        }
        Repeater {
            model: 15
            delegate: Rectangle {
                required property int index
                width: parent.width
                height: 1
                y: index * (parent.height / 15)
                color: Theme.border
                opacity: 0.28
            }
        }
        Text {
            anchors.centerIn: parent
            text: qsTr("Map canvas placeholder")
            color: Theme.muted
            font.pixelSize: Theme.fontTitle
        }
        Rectangle {
            x: 18
            y: 18
            width: 185
            height: 38
            radius: Theme.radiusControl
            color: Theme.surface
            border.color: Theme.border
            Text {
                anchors.centerIn: parent
                text: qsTr("Overworld  ·  Zoom 1×")
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }
        }
        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 18
            y: 18
            width: 42
            height: 42
            radius: Theme.radiusPill
            color: Theme.surface
            AppIcon {
                anchors.centerIn: parent
                width: 20
                height: 20
                name: "locate"
                iconSize: width
                iconColor: Theme.accent
            }
        }
        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 18
            width: 210
            height: 52
            radius: Theme.radiusCard
            color: Theme.surface
            border.color: Theme.border
            Text {
                anchors.centerIn: parent
                text: qsTr("Layers  ·  Markers  ·  Entities")
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }
        }
    }
}

import BedrockMap 1.0
import QtQuick
import QtQuick.Layouts
import "controls"

Rectangle {
    id: root
    property var controller
    property bool headerVisible: true
    readonly property string headerTitle: qsTr("Current area · 3D")
    signal closeRequested

    color: Theme.surface
    border.width: 0

    Rectangle {
        width: 1
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            visible: root.headerVisible
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: Theme.surface

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: root.headerTitle
                color: Theme.text
                font.pixelSize: Theme.fontSubtitle
                font.bold: true
            }

            AppIconButton {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                iconName: "close"
                label: qsTr("Close 3D view")
                buttonSize: 32
                onClicked: root.closeRequested()
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }
        }

        VoxelPreview {
            Layout.fillWidth: true
            Layout.fillHeight: true
            controller: root.controller
        }
    }
}

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

        AppTitleBar {
            visible: root.headerVisible
            embedded: true
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            title: root.headerTitle
            leftInset: 12
            rightInset: 8
            actions: [
                AppIconButton {
                    iconName: "close"
                    label: qsTr("Close 3D view")
                    buttonSize: 32
                    onClicked: root.closeRequested()
                }
            ]
        }

        VoxelPreview {
            Layout.fillWidth: true
            Layout.fillHeight: true
            controller: root.controller
        }
    }
}

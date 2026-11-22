pragma ComponentBehavior: Bound

import BedrockMap 1.0
import QtQuick
import QtQuick.Layouts
import "controls"

Item {
    id: root

    required property var view
    readonly property var camera: view ? view.camera : null
    property bool controlsVisible: true
    property bool settingsOpen: false
    property bool directionControlsVisible: false

    visible: controlsVisible && !!camera
    z: 5

    component ViewButton: AppIconButton {
        Layout.preferredWidth: controlSize
        Layout.preferredHeight: controlSize
    }

    AppCard {
        id: settingsPanel
        visible: root.settingsOpen
        anchors.left: settingsToolbar.left
        anchors.bottom: settingsToolbar.top
        anchors.bottomMargin: Theme.gapSmall
        width: Math.min(Theme.ui(360), Math.max(Theme.ui(260), settingsToolbar.width))
        height: settingsColumn.implicitHeight + Theme.gapLarge * 2

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            preventStealing: true
        }

        Column {
            id: settingsColumn
            x: Theme.gapLarge
            y: Theme.gapLarge
            width: Math.max(0, parent.width - Theme.gapLarge * 2)
            spacing: Theme.gapSmall

            Flow {
                width: parent.width
                spacing: Theme.gapSmall

                AppToggle {
                    id: orthoToggle
                    text: qsTr("Orthographic projection")
                    onToggled: if (root.camera)
                    root.camera.orthoMode = checked
                    Binding {
                        target: orthoToggle
                        property: "checked"
                        value: root.camera ? root.camera.orthoMode : false
                    }
                }
                AppToggle {
                    id: axesToggle
                    text: qsTr("Show axes")
                    onToggled: if (root.view)
                    root.view.showAxes = checked
                    Binding {
                        target: axesToggle
                        property: "checked"
                        value: root.view ? root.view.showAxes : false
                    }
                }
                AppToggle {
                    id: rotationToggle
                    text: qsTr("Lock rotation")
                    onToggled: if (root.camera)
                    root.camera.rotationLocked = checked
                    Binding {
                        target: rotationToggle
                        property: "checked"
                        value: root.camera ? root.camera.rotationLocked : false
                    }
                }
                AppToggle {
                    id: directionToggle
                    text: qsTr("Show direction controls")
                    onToggled: root.directionControlsVisible = checked
                    Binding {
                        target: directionToggle
                        property: "checked"
                        value: root.directionControlsVisible
                    }
                }
            }
        }
    }

    AppCard {
        id: settingsToolbar
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: Theme.gapLarge
        width: settingsRow.implicitWidth + Theme.gapSmall * 2
        height: settingsRow.height + Theme.gapSmall * 2

        Row {
            id: settingsRow
            x: Theme.gapSmall
            height: Theme.touch ? Theme.controlHeight : 34
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.gapTiny

            ViewButton {
                iconName: "refresh"
                label: qsTr("Reset view")
                onClicked: root.camera.resetView()
            }
            ViewButton {
                iconName: "settings"
                label: qsTr("View settings")
                selected: root.settingsOpen
                onClicked: root.settingsOpen = !root.settingsOpen
            }
        }
    }

    AppCard {
        id: directionTray
        visible: root.directionControlsVisible
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.gapLarge
        width: directionGrid.implicitWidth + Theme.gapSmall * 2
        height: directionGrid.implicitHeight + Theme.gapSmall * 2

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            preventStealing: true
        }

        Grid {
            id: directionGrid
            x: Theme.gapSmall
            y: Theme.gapSmall
            columns: 3
            columnSpacing: Theme.gapTiny
            rowSpacing: 0

            Item {
                width: Theme.iconButtonSize
                height: width
            }
            ViewButton {
                iconName: "caret_up"
                label: qsTr("Rotate up")
                onClicked: root.camera.rotateView(0, -90)
            }
            Item {
                width: Theme.iconButtonSize
                height: width
            }

            ViewButton {
                iconName: "caret_left"
                label: qsTr("Rotate left")
                onClicked: root.camera.rotateView(-90, 0)
            }
            ViewButton {
                iconName: "front"
                label: qsTr("Face front")
                onClicked: root.camera.focusFrontFace()
            }
            ViewButton {
                iconName: "caret_right"
                label: qsTr("Rotate right")
                onClicked: root.camera.rotateView(90, 0)
            }

            Item {
                width: Theme.iconButtonSize
                height: width
            }
            ViewButton {
                iconName: "caret_down"
                label: qsTr("Rotate down")
                onClicked: root.camera.rotateView(0, 90)
            }
            Item {
                width: Theme.iconButtonSize
                height: width
            }
        }
    }
}

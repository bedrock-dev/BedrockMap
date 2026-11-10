pragma ComponentBehavior: Bound

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

Dialog {
    id: root

    property var dimensions: []
    signal dimensionAccepted(int dimension)

    modal: true
    width: Math.min(380, parent ? parent.width - 32 : 380)
    height: Math.min(380, parent ? parent.height - 48 : 380)
    padding: Theme.cardPadding
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0

    function openFor(options, preferredDimension) {
        dimensions = options
        let selected = 0
        for (let index = 0; index < dimensions.length; ++index) {
            if (Number(dimensions[index].id) === Number(preferredDimension)) {
                selected = index
                break
            }
        }
        dimensionList.currentIndex = dimensions.length > 0 ? selected : -1
        open()
        Qt.callLater(function () {
            if (dimensionList.currentIndex >= 0)
                dimensionList.positionViewAtIndex(dimensionList.currentIndex, ListView.Contain)
        })
    }

    function acceptSelection() {
        if (dimensionList.currentIndex < 0 || dimensionList.currentIndex >= dimensions.length)
            return
        dimensionAccepted(Number(dimensions[dimensionList.currentIndex].id))
        close()
    }

    background: Rectangle {
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: Theme.gapMedium

        Text {
            Layout.fillWidth: true
            text: qsTr("Custom dimension")
            color: Theme.text
            font.pixelSize: Theme.fontTitle
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusSmall
            color: Theme.surfaceAlt
            border.width: Theme.borderWidth
            border.color: Theme.border

            ListView {
                id: dimensionList
                anchors.fill: parent
                anchors.margins: Theme.ui(3)
                clip: true
                spacing: 1
                model: root.dimensions
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {
                    id: dimensionScrollBar
                    policy: ScrollBar.AsNeeded
                    background: Rectangle {
                        color: "transparent"
                    }
                    contentItem: Rectangle {
                        implicitWidth: 6
                        radius: Theme.radiusSmall
                        color: Theme.border
                        opacity: dimensionScrollBar.pressed || dimensionScrollBar.hovered ? 1.0 : 0.6
                    }
                }

                delegate: ItemDelegate {
                    id: dimensionOption
                    required property int index
                    required property var modelData

                    width: ListView.view.width
                    height: Theme.ui(36)
                    highlighted: ListView.isCurrentItem
                    onClicked: ListView.view.currentIndex = index
                    onDoubleClicked: {
                        ListView.view.currentIndex = index
                        root.acceptSelection()
                    }

                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: dimensionOption.highlighted ? Theme.accentSoft : "transparent"
                        border.width: dimensionOption.highlighted ? Theme.borderWidth : 0
                        border.color: Theme.accent
                    }

                    contentItem: RowLayout {
                        width: dimensionOption.availableWidth
                        spacing: Theme.gapSmall

                        Text {
                            Layout.fillWidth: true
                            text: dimensionOption.modelData.name
                            color: Theme.text
                            font.pixelSize: Theme.fontSubtitle
                            elide: Text.ElideRight
                        }

                        Text {
                            text: qsTr("ID: %1").arg(dimensionOption.modelData.id)
                            color: Theme.muted
                            font.pixelSize: Theme.fontBody
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapSmall

            Item {
                Layout.fillWidth: true
            }

            AppButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }

            AppButton {
                text: qsTr("Apply")
                primary: true
                onClicked: root.acceptSelection()
            }
        }
    }
}

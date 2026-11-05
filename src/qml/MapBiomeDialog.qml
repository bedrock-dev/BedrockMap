import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BedrockMap 1.0
import "controls"

Dialog {
    id: root

    modal: true
    width: Math.min(400, parent ? parent.width - 32 : 400)
    height: Math.min(420, parent ? parent.height - 48 : 420)
    padding: Theme.cardPadding
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0

    property var options: []
    property var allOptions: []
    signal biomeAccepted(int value)

    function filterOptions(preferredValue) {
        const query = biomeSearch.text.trim().toLocaleLowerCase()
        const priorValue = preferredValue !== undefined ? Number(preferredValue) : (biomeList.currentIndex >= 0
                                                                                    && biomeList.currentIndex
                                                                                    < options.length ? Number(
                                                                                                           options[biomeList.currentIndex].value) :
                                                                                                       -1)
        const filtered = query.length === 0 ? allOptions : allOptions.filter(function (option) {
            return String(option.name).toLocaleLowerCase().indexOf(query) >= 0
        })
        options = filtered

        let selected = 0
        for (let index = 0; index < filtered.length; ++index) {
            if (Number(filtered[index].value) === priorValue) {
                selected = index
                break
            }
        }
        biomeList.currentIndex = filtered.length > 0 ? selected : -1
        Qt.callLater(function () {
            if (biomeList.currentIndex >= 0)
                biomeList.positionViewAtIndex(biomeList.currentIndex, ListView.Contain)
        })
    }

    function openFor(nextOptions, preferredValue) {
        allOptions = nextOptions
        biomeSearch.text = ""
        filterOptions(preferredValue)
        open()
    }

    function acceptSelection() {
        if (biomeList.currentIndex < 0 || biomeList.currentIndex >= options.length)
            return
        biomeAccepted(Number(options[biomeList.currentIndex].value))
        close()
    }

    background: Rectangle {
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: Theme.gapMedium

        Text {
            Layout.fillWidth: true
            text: qsTr("Set biome")
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.bold: true
        }

        AppTextField {
            id: biomeSearch
            Layout.fillWidth: true
            placeholderText: qsTr("Filter biomes")
            onTextChanged: root.filterOptions()
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusSmall
            color: Theme.surfaceAlt
            border.width: 1
            border.color: Theme.border

            ListView {
                id: biomeList
                anchors.fill: parent
                anchors.margins: 3
                clip: true
                spacing: 1
                model: root.options
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {
                    id: biomeScrollBar
                    policy: ScrollBar.AsNeeded
                    background: Rectangle {
                        color: "transparent"
                    }
                    contentItem: Rectangle {
                        implicitWidth: 6
                        radius: Theme.radiusSmall
                        color: Theme.border
                        opacity: biomeScrollBar.pressed || biomeScrollBar.hovered ? 1.0 : 0.6
                    }
                }

                delegate: ItemDelegate {
                    id: biomeOption
                    required property int index
                    required property var modelData

                    width: ListView.view.width
                    height: 32
                    highlighted: ListView.isCurrentItem
                    onClicked: ListView.view.currentIndex = index

                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: biomeOption.highlighted ? Theme.accentSoft : "transparent"
                        border.width: biomeOption.highlighted ? 1 : 0
                        border.color: Theme.accent
                    }

                    contentItem: Item {
                        RowLayout {
                            width: parent.width
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10

                            Rectangle {
                                Layout.preferredWidth: 20
                                Layout.preferredHeight: 20
                                radius: 3
                                color: biomeOption.modelData.color
                                border.width: 1
                                border.color: Theme.border
                            }

                            Text {
                                Layout.preferredWidth: 42
                                text: biomeOption.modelData.value
                                color: Theme.muted
                                font.pixelSize: Theme.fontSubtitle
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignVCenter
                            }

                            Text {
                                Layout.fillWidth: true
                                text: biomeOption.modelData.name
                                color: Theme.text
                                font.pixelSize: Theme.fontSubtitle
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
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

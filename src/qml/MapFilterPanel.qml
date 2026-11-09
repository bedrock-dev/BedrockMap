import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

Popup {
    id: root

    property var controller
    property var anchorItem
    property bool blockBlackMode: true
    property bool biomeBlackMode: true
    property bool actorBlackMode: true
    property bool layerEnabled: false
    property int layer: 64

    function formatList(values) {
        if (!values || values.length === undefined)
            return ""
        const parts = []
        for (let index = 0; index < values.length; ++index)
            parts.push(String(values[index]))
        return parts.join(", ")
    }

    function splitList(text) {
        const result = []
        const values = String(text).split(/[\s,;]+/)
        for (let index = 0; index < values.length; ++index) {
            const value = values[index].trim()
            if (value.length > 0 && result.indexOf(value) < 0)
                result.push(value)
        }
        return result
    }

    function splitBiomeIds(text) {
        const result = []
        const values = splitList(text)
        for (let index = 0; index < values.length; ++index) {
            const value = Number(values[index])
            if (Number.isInteger(value) && result.indexOf(value) < 0)
                result.push(value)
        }
        return result
    }

    function populate(options) {
        options = options || ({})
        blockBlackMode = options.blockBlackMode !== undefined ? Boolean(options.blockBlackMode) : true
        biomeBlackMode = options.biomeBlackMode !== undefined ? Boolean(options.biomeBlackMode) : true
        actorBlackMode = options.actorBlackMode !== undefined ? Boolean(options.actorBlackMode) : true
        layerEnabled = options.layerEnabled !== undefined ? Boolean(options.layerEnabled) : false
        layer = options.layer !== undefined ? Number(options.layer) : 64
        blockField.text = formatList(options.blockList)
        biomeField.text = formatList(options.biomeList)
        actorField.text = formatList(options.actorList)
    }

    function refresh() {
        populate(controller ? controller.mapFilterOptions() : null)
    }

    function resetDraft() {
        populate(controller ? controller.defaultMapFilterOptions() : null)
    }

    function applyDraft() {
        if (!controller)
            return
        const applied = controller.applyMapFilterOptions({
                                                             blockBlackMode: blockBlackMode,
                                                             blockList: splitList(blockField.text),
                                                             biomeBlackMode: biomeBlackMode,
                                                             biomeList: splitBiomeIds(biomeField.text),
                                                             actorBlackMode: actorBlackMode,
                                                             actorList: splitList(actorField.text),
                                                             layerEnabled: layerEnabled,
                                                             layer: layer
                                                         })
        if (applied)
            close()
    }

    function positionNearAnchor() {
        if (!anchorItem || !parent || !opened)
            return
        const origin = anchorItem.mapToItem(parent, 0, 0)
        const margin = 8
        const gap = 6
        const maxX = Math.max(margin, parent.width - width - margin)
        x = Math.min(Math.max(margin, origin.x + anchorItem.width - width), maxX)

        const below = origin.y + anchorItem.height + gap
        y = below + height <= parent.height - margin ? below : Math.max(margin, origin.y - height - gap)
    }

    function openFor(item) {
        anchorItem = item
        refresh()
        open()
        Qt.callLater(positionNearAnchor)
    }

    parent: Overlay.overlay
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: Theme.cardPadding
    width: parent ? Math.max(0, Math.min(360, parent.width - 16)) : 360
    height: Math.min(popupColumn.implicitHeight + topPadding + bottomPadding, parent ? parent.height - 16 : 600)

    onOpened: positionNearAnchor()
    onHeightChanged: if (opened)
                         positionNearAnchor()
    onControllerChanged: if (!controller && opened)
                             close()

    background: AppCard {}

    contentItem: Flickable {
        id: contentFlick
        implicitWidth: 320
        implicitHeight: popupColumn.implicitHeight
        contentWidth: width
        contentHeight: popupColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: popupColumn
            width: contentFlick.width
            spacing: Theme.gapMedium

            Text {
                Layout.fillWidth: true
                text: qsTr("Map filters")
                color: Theme.text
                font.pixelSize: Theme.fontTitle
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.borderWidth
                color: Theme.border
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gapSmall

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Blocks")
                    color: Theme.text
                    font.pixelSize: Theme.fontSubtitle
                }

                AppSegmentedControl {
                    Layout.preferredWidth: 142
                    Layout.preferredHeight: Theme.buttonHeight
                    options: [qsTr("Exclude"), qsTr("Include")]
                    currentIndex: root.blockBlackMode ? 0 : 1
                    onActivated: index => root.blockBlackMode = index === 0
                }
            }

            AppTextArea {
                id: blockField
                Layout.fillWidth: true
                Layout.preferredHeight: 72
                placeholderText: qsTr("Block IDs, separated by commas")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gapSmall

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Biomes")
                    color: Theme.text
                    font.pixelSize: Theme.fontSubtitle
                }

                AppSegmentedControl {
                    Layout.preferredWidth: 142
                    Layout.preferredHeight: Theme.buttonHeight
                    options: [qsTr("Exclude"), qsTr("Include")]
                    currentIndex: root.biomeBlackMode ? 0 : 1
                    onActivated: index => root.biomeBlackMode = index === 0
                }
            }

            AppTextArea {
                id: biomeField
                Layout.fillWidth: true
                Layout.preferredHeight: 72
                placeholderText: qsTr("Biome IDs, separated by commas")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gapSmall

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Entities")
                    color: Theme.text
                    font.pixelSize: Theme.fontSubtitle
                }

                AppSegmentedControl {
                    Layout.preferredWidth: 142
                    Layout.preferredHeight: Theme.buttonHeight
                    options: [qsTr("Exclude"), qsTr("Include")]
                    currentIndex: root.actorBlackMode ? 0 : 1
                    onActivated: index => root.actorBlackMode = index === 0
                }
            }

            AppTextArea {
                id: actorField
                Layout.fillWidth: true
                Layout.preferredHeight: 72
                placeholderText: qsTr("Entity keys, separated by commas")
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.borderWidth
                color: Theme.border
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gapSmall

                AppToggle {
                    label: qsTr("Fixed Y layer")
                    active: root.layerEnabled
                    onToggled: root.layerEnabled = active
                }

                Item {
                    Layout.fillWidth: true
                }

                AppSpinBox {
                    Layout.preferredWidth: 112
                    from: -64
                    to: 320
                    value: root.layer
                    enabled: root.layerEnabled
                    onValueModified: root.layer = value
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gapSmall

                Item {
                    Layout.fillWidth: true
                }

                AppButton {
                    text: qsTr("Reset")
                    onClicked: root.resetDraft()
                }

                AppButton {
                    text: qsTr("Cancel")
                    onClicked: root.close()
                }

                AppButton {
                    text: qsTr("Apply")
                    primary: true
                    onClicked: root.applyDraft()
                }
            }
        }
    }
}

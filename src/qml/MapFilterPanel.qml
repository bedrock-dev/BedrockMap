import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

AppCard {
    id: root

    property var controller
    property bool blockBlackMode: true
    property bool biomeBlackMode: true
    property bool actorBlackMode: true
    property bool layerEnabled: false
    property int fixedLayer: 64
    readonly property real naturalHeight: popupColumn.implicitHeight + Theme.gapLarge * 2
    signal closeRequested

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
        fixedLayer = options.layer !== undefined ? Number(options.layer) : 64
        blockFilter.text = formatList(options.blockList)
        biomeFilter.text = formatList(options.biomeList)
        actorFilter.text = formatList(options.actorList)
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
                                                             blockList: splitList(blockFilter.text),
                                                             biomeBlackMode: biomeBlackMode,
                                                             biomeList: splitBiomeIds(biomeFilter.text),
                                                             actorBlackMode: actorBlackMode,
                                                             actorList: splitList(actorFilter.text),
                                                             layerEnabled: layerEnabled,
                                                             layer: fixedLayer
                                                         })
        if (applied)
            root.closeRequested()
    }

    component FilterEditor: ColumnLayout {
        id: editor

        property string title
        property string placeholderText
        property bool blackMode
        property alias text: field.text
        signal modeChanged(bool blackMode)

        Layout.fillWidth: true
        spacing: Theme.gapSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapSmall

            Text {
                Layout.fillWidth: true
                text: editor.title
                color: Theme.text
                font.pixelSize: Theme.fontMedium
            }

            AppSegmentedControl {
                Layout.preferredWidth: 142
                Layout.preferredHeight: Theme.buttonHeight
                options: [qsTr("Exclude"), qsTr("Include")]
                currentIndex: editor.blackMode ? 0 : 1
                onActivated: index => editor.modeChanged(index === 0)
            }
        }

        AppTextArea {
            id: field
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            placeholderText: editor.placeholderText
        }
    }

    onVisibleChanged: if (visible)
                          refresh()
    onControllerChanged: if (!controller && visible)
                             root.closeRequested()

    Flickable {
        id: contentFlick
        anchors.fill: parent
        contentWidth: width
        contentHeight: popupColumn.implicitHeight + Theme.gapLarge * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        ColumnLayout {
            id: popupColumn
            x: Theme.gapLarge
            y: Theme.gapLarge
            width: contentFlick.width - Theme.gapLarge * 2
            spacing: Theme.gapMedium

            FilterEditor {
                id: blockFilter
                title: qsTr("Blocks")
                placeholderText: qsTr("Block IDs, separated by commas")
                blackMode: root.blockBlackMode
                onModeChanged: blackMode => root.blockBlackMode = blackMode
            }

            FilterEditor {
                id: biomeFilter
                title: qsTr("Biomes")
                placeholderText: qsTr("Biome IDs, separated by commas")
                blackMode: root.biomeBlackMode
                onModeChanged: blackMode => root.biomeBlackMode = blackMode
            }

            FilterEditor {
                id: actorFilter
                title: qsTr("Entities")
                placeholderText: qsTr("Entity keys, separated by commas")
                blackMode: root.actorBlackMode
                onModeChanged: blackMode => root.actorBlackMode = blackMode
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
                    text: qsTr("Fixed Y layer")
                    checked: root.layerEnabled
                    onToggled: root.layerEnabled = checked
                }

                Item {
                    Layout.fillWidth: true
                }

                AppSpinBox {
                    Layout.preferredWidth: 112
                    from: -64
                    to: 320
                    value: root.fixedLayer
                    enabled: root.layerEnabled
                    onValueModified: root.fixedLayer = value
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
                    onClicked: root.closeRequested()
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

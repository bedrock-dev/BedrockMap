import BedrockMap 1.0
import QtQuick

Rectangle {
    id: root
    property var options: []
    property int currentIndex: 0
    readonly property int optionCount: options.length
    readonly property real buttonWidth: optionCount > 0 ? Math.max(48, (width - 6 - 2 * Math.max(0, optionCount - 1))
                                                                   / optionCount) : 0
    signal activated(int index)

    implicitHeight: Theme.controlHeight + 6
    radius: Theme.radiusControl
    color: Theme.surfaceAlt
    border.width: 1
    border.color: Theme.border
    clip: true

    Flickable {
        id: tabFlick
        anchors.fill: parent
        anchors.margins: 3
        contentWidth: row.width
        contentHeight: height
        clip: true
        interactive: contentWidth > width
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        DragHandler {
            target: null
            xAxis.enabled: true
            yAxis.enabled: false
            grabPermissions: PointerHandler.CanTakeOverFromAnything
            property real startContentX: 0

            onActiveChanged: {
                if (active)
                    startContentX = tabFlick.contentX
            }
            onTranslationChanged: {
                if (!active)
                    return
                const maxX = Math.max(0, tabFlick.contentWidth - tabFlick.width)
                tabFlick.contentX = Math.max(0, Math.min(maxX, startContentX - translation.x))
            }
        }

        Row {
            id: row
            height: parent.height
            spacing: 2
            Repeater {
                model: root.options
                delegate: AppButton {
                    required property string modelData
                    required property int index
                    text: modelData
                    compact: true
                    ghost: true
                    bordered: false
                    active: root.currentIndex === index
                    width: root.buttonWidth
                    height: row.height
                    onClicked: root.activated(index)
                }
            }
        }
    }
}

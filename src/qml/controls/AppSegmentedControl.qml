import BedrockMap 1.0
import QtQuick

Rectangle {
    id: root
    property var options: []
    property int currentIndex: 0
    property bool fitLabels: false
    readonly property int optionCount: options.length
    readonly property real buttonWidth: optionCount > 0 ? Math.max(Theme.ui(48), (width - Theme.segmentedPadding * 2
                                                                                  - Theme.ui(2) * Math.max(0,
                                                                                                           optionCount
                                                                                                           - 1)) / optionCount) :
                                                          0
    signal activated(int index)

    function ensureCurrentVisible() {
        if (!fitLabels)
            return
        const button = optionButtons.itemAt(currentIndex)
        if (!button)
            return
        const targetX = button.x < tabFlick.contentX ? button.x : Math.max(tabFlick.contentX, button.x + button.width
                                                                           - tabFlick.width)
        tabFlick.contentX = Math.max(0, Math.min(targetX, tabFlick.contentWidth - tabFlick.width))
    }
    onCurrentIndexChanged: Qt.callLater(ensureCurrentVisible)
    onOptionsChanged: Qt.callLater(ensureCurrentVisible)
    onWidthChanged: Qt.callLater(ensureCurrentVisible)

    implicitHeight: Theme.segmentedHeight
    implicitWidth: fitLabels ? row.implicitWidth + Theme.segmentedPadding * 2 : 0
    radius: Theme.radiusControl
    color: Theme.surfaceAlt
    border.width: Theme.borderWidth
    border.color: Theme.border
    clip: true

    Flickable {
        id: tabFlick
        anchors.fill: parent
        anchors.leftMargin: Theme.segmentedPadding
        anchors.rightMargin: Theme.segmentedPadding
        anchors.topMargin: Theme.touch ? 0 : Theme.segmentedPadding
        anchors.bottomMargin: Theme.touch ? 0 : Theme.segmentedPadding
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
            spacing: Theme.ui(2)
            Repeater {
                id: optionButtons
                model: root.options
                delegate: AppButton {
                    required property string modelData
                    required property int index
                    text: modelData
                    compact: true
                    ghost: true
                    bordered: false
                    active: root.currentIndex === index
                    width: root.fitLabels ? implicitWidth : root.buttonWidth
                    height: row.height
                    visualHeight: Theme.touch ? height - Theme.segmentedPadding * 2 : height
                    onClicked: root.activated(index)
                }
            }
        }
    }
}

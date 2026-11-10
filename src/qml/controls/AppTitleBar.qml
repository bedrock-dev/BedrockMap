import BedrockMap 1.0
import QtQuick
import QtQuick.Layouts

/// Shared title bar shell. The title stays on the left; callers append their
/// own actions through the default property slot.
Rectangle {
    id: root

    property bool embedded: false
    property string title
    property string subtitle
    property alias actions: actionRow.data
    property bool showBottomBorder: true
    property bool backVisible: false
    property real leftInset: 0
    property real rightInset: 0
    signal backRequested

    implicitHeight: embedded ? 48 : Theme.topbarHeight
    color: embedded ? "transparent" : Theme.surface

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.embedded ? root.leftInset : Theme.gapSection + root.leftInset
        anchors.rightMargin: root.embedded ? root.rightInset : Theme.gapSection + root.rightInset
        spacing: Theme.gapSmall

        AppIconButton {
            visible: root.backVisible
            iconName: "back"
            label: qsTr("Back")
            buttonSize: root.embedded ? Theme.iconButtonSmallSize : Theme.iconButtonSize
            Layout.alignment: Qt.AlignVCenter
            onClicked: root.backRequested()
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.gapTiny

            Text {
                Layout.fillWidth: true
                text: root.title
                color: Theme.text
                font.pixelSize: root.embedded ? Theme.fontSubtitle :
                                                Theme.compact ? Theme.fontTitle : Theme.fontDisplay
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: root.subtitle.length > 0
                text: root.subtitle
                color: Theme.muted
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
        }

        RowLayout {
            id: actionRow
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.gapSmall
        }
    }

    Rectangle {
        visible: root.showBottomBorder
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.borderWidth
        color: Theme.border
    }
}

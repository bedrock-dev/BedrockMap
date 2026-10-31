import BedrockMap 1.0
import QtQuick
import QtQuick.Layouts

/// Shared title bar shell. The title stays on the left; callers append their
/// own actions through the default property slot.
Rectangle {
    id: root
    property string title
    property alias actions: actionRow.data
    property bool showBottomBorder: true
    property bool backVisible: false
    property real leftInset: 0
    property real rightInset: 0
    signal backRequested

    implicitHeight: Theme.topbarHeight
    color: Theme.surface

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: (Theme.compact ? 12 : 28) + root.leftInset
        anchors.rightMargin: (Theme.compact ? 8 : 22) + root.rightInset
        spacing: Theme.gapSmall

        AppIconButton {
            visible: root.backVisible
            iconName: "back"
            label: qsTr("Back")
            buttonSize: 34
            onClicked: root.backRequested()
        }
        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.compact ? Theme.fontTitle : Theme.fontDisplay
            font.bold: true
            elide: Text.ElideRight
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
        height: 1
        color: Theme.border
    }
}

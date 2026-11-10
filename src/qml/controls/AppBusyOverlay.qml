import BedrockMap 1.0
import QtQuick

Rectangle {
    id: root
    property bool active: false
    property string text
    property string detail

    visible: active
    color: Theme.overlay

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        preventStealing: true
    }

    AppCard {
        anchors.centerIn: parent
        width: Math.min(400, parent.width - Theme.gapSection * 2)
        height: root.detail.length > 0 ? 128 : 104
        Column {
            anchors.fill: parent
            anchors.margins: Theme.gapLarge
            spacing: Theme.gapLarge
            Text {
                width: parent.width
                text: root.text.length > 0 ? root.text : qsTr("Working…")
                color: Theme.text
                font.pixelSize: Theme.fontSubtitle
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }
            Text {
                width: parent.width
                visible: root.detail.length > 0
                text: root.detail
                color: Theme.muted
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideMiddle
                horizontalAlignment: Text.AlignHCenter
            }
            Rectangle {
                width: parent.width
                height: 8
                radius: height / 2
                color: Theme.surfaceAlt
                clip: true
                Rectangle {
                    id: progressFill
                    width: parent.width * 0.28
                    height: parent.height
                    radius: height / 2
                    color: Theme.accent
                    SequentialAnimation on x {
                        running: root.visible
                        loops: Animation.Infinite
                        NumberAnimation {
                            from: -progressFill.width
                            to: progressFill.parent.width
                            duration: 1100
                            easing.type: Easing.InOutQuad
                        }
                        PauseAnimation {
                            duration: 100
                        }
                    }
                }
            }
            Text {
                width: parent.width
                text: qsTr("Please wait…")
                color: Theme.muted
                font.pixelSize: Theme.fontCaption
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}

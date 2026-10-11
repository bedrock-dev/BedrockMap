import QtQuick
import QtQuick.Controls

Item {
    id: workspace
    property var colors
    property string worldTitle: qsTr("World workspace")
    property string worldPath: ""
    signal closeRequested()

    Canvas { id: canvas; anchors.fill: parent; onPaint: { var ctx = getContext("2d"); ctx.fillStyle = colors.background; ctx.fillRect(0, 0, width, height); ctx.strokeStyle = colors.border; ctx.globalAlpha = 0.22; for (var x = 0; x < width; x += 32) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke() } for (var y = 0; y < height; y += 32) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke() } } }

    Rectangle {
        id: layerPanel
        z: 1; width: 238; anchors.top: parent.top; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 14; radius: 12; color: colors.surface; border.color: colors.border
        Column { anchors.fill: parent; anchors.margins: 12; spacing: 8
            Text { text: qsTr("Layers"); color: colors.text; font.pixelSize: 14; font.bold: true }
            Text { text: qsTr("View"); color: colors.muted; font.pixelSize: 10 }
            Flow { width: parent.width; spacing: 5
                ToolToggle { label: qsTr("Grid"); active: true }
                ToolToggle { label: qsTr("Coords") }
            }
            Text { text: qsTr("Dimension"); color: colors.muted; font.pixelSize: 10 }
            TabBar { id: dimensionBar; width: parent.width; height: 30; background: Rectangle { color: colors.surfaceAlt; radius: 6 }
                StyledTabButton { text: qsTr("Overworld") }
                StyledTabButton { text: qsTr("Nether") }
                StyledTabButton { text: qsTr("The End") }
            }
            Text { text: qsTr("Layer"); color: colors.muted; font.pixelSize: 10 }
            TabBar { id: layerBar; width: parent.width; height: 30; background: Rectangle { color: colors.surfaceAlt; radius: 6 }
                StyledTabButton { text: qsTr("Terrain") }
                StyledTabButton { text: qsTr("Biomes") }
            }
            Text { text: qsTr("Overlays"); color: colors.muted; font.pixelSize: 10 }
            Flow { width: parent.width; spacing: 5
                ToolToggle { label: qsTr("Slime") }
                ToolToggle { label: qsTr("Entities"); active: true }
                ToolToggle { label: qsTr("Villages") }
                ToolToggle { label: qsTr("HSA") }
            }
            Rectangle { width: parent.width; height: 1; color: colors.border }
            Text { text: worldPath; color: colors.muted; font.pixelSize: 10; elide: Text.ElideMiddle; width: parent.width }
        }
    }

    Rectangle {
        z: 1; anchors.top: parent.top; anchors.left: parent.left; anchors.right: layerPanel.left; anchors.margins: 14; height: 44; radius: 10; color: colors.surface; border.color: colors.border
        Row { anchors.fill: parent; anchors.margins: 5; spacing: 6
            ToolButton { label: "＋"; hint: qsTr("Zoom in") }
            ToolButton { label: "−"; hint: qsTr("Zoom out") }
            ToolButton { label: "⌖"; hint: qsTr("Center map") }
            Item { width: 1; height: 1 }
            Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Canvas placeholder"); color: colors.muted; font.pixelSize: 11 }
            Item { width: 1; height: 1 }
            ToolButton { label: "⌘"; hint: qsTr("Save"); onClicked: saveNotice.visible = true }
            ToolButton { label: "×"; hint: qsTr("Close map"); danger: true; onClicked: workspace.closeRequested() }
        }
    }

    Rectangle { id: saveNotice; z: 2; visible: false; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 24; width: noticeText.implicitWidth + 32; height: 36; radius: 18; color: colors.accentSoft; border.color: colors.accent
        Text { id: noticeText; anchors.centerIn: parent; text: qsTr("Save is ready to be connected"); color: colors.accent; font.pixelSize: 12 }
        Timer { interval: 1800; running: saveNotice.visible; repeat: false; onTriggered: saveNotice.visible = false }
    }

    component ToolButton: Rectangle {
        property string label
        property string hint
        property bool danger: false
        signal clicked()
        width: 34; height: 34; radius: 7; color: mouse.containsMouse ? (danger ? "#542d35" : colors.surfaceAlt) : "transparent"
        Text { anchors.centerIn: parent; text: label; color: danger ? "#ed7785" : colors.text; font.pixelSize: 18 }
        MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; onClicked: parent.clicked() }
    }

    component ToolToggle: Rectangle {
        property string label
        property bool active: false
        width: Math.max(68, labelText.implicitWidth + 20); height: 28; radius: 7; color: active ? colors.accentSoft : colors.surfaceAlt; border.color: active ? colors.accent : colors.border
        Text { id: labelText; anchors.centerIn: parent; text: label; color: active ? colors.accent : colors.muted; font.pixelSize: 11 }
        MouseArea { id: toggleMouse; anchors.fill: parent; hoverEnabled: true; onClicked: parent.active = !parent.active }
    }

    component StyledTabButton: TabButton {
        font.pixelSize: 10
        contentItem: Text {
            text: parent.text
            color: parent.checked ? colors.accent : colors.muted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 6
            color: parent.checked ? colors.accentSoft : "transparent"
        }
    }
}

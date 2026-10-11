import QtQuick

Item {
    property var colors
    Rectangle { anchors.fill: parent; anchors.margins: 12; radius: 14; color: colors.map; border.color: colors.border
        Repeater { model: 24; delegate: Rectangle { required property int index; width: 1; height: parent.height; x: index * (parent.width / 24); color: colors.border; opacity: 0.28 } }
        Repeater { model: 15; delegate: Rectangle { required property int index; width: parent.width; height: 1; y: index * (parent.height / 15); color: colors.border; opacity: 0.28 } }
        Text { anchors.centerIn: parent; text: qsTr("Map canvas placeholder"); color: colors.muted; font.pixelSize: 18 }
        Rectangle { x: 18; y: 18; width: 185; height: 38; radius: 9; color: colors.surface; border.color: colors.border; Text { anchors.centerIn: parent; text: qsTr("Overworld  ·  Zoom 1×"); color: colors.text; font.pixelSize: 11 } }
        Rectangle { anchors.right: parent.right; anchors.rightMargin: 18; y: 18; width: 42; height: 42; radius: 21; color: colors.surface; Text { anchors.centerIn: parent; text: "⌖"; color: colors.accent; font.pixelSize: 20 } }
        Rectangle { anchors.left: parent.left; anchors.leftMargin: 18; anchors.bottom: parent.bottom; anchors.bottomMargin: 18; width: 210; height: 52; radius: 10; color: colors.surface; border.color: colors.border; Text { anchors.centerIn: parent; text: qsTr("Layers  ·  Markers  ·  Entities"); color: colors.text; font.pixelSize: 11 } }
    }
}

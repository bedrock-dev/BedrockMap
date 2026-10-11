import QtQuick
import QtQuick.Controls

Item {
    id: page
    property var colors
    property bool hasActiveWorld: false
    property int currentTab: 0
    readonly property var tabNames: [qsTr("level.dat"), qsTr("Players"), qsTr("Villages"), qsTr("Portals"), qsTr("Structures"), qsTr("Maps"), qsTr("Other")]

    Column { anchors.fill: parent; anchors.margins: 22; spacing: 14
        Text { text: qsTr("Global data"); color: colors.text; font.pixelSize: 18; font.bold: true }
        TabBar { id: tabBar; width: parent.width; height: 42; currentIndex: page.currentTab; onCurrentIndexChanged: page.currentTab = currentIndex; background: Rectangle { radius: 9; color: colors.surface; border.color: colors.border }
            Repeater { model: page.tabNames; delegate: TabButton { required property string modelData; text: modelData; width: Math.max(74, implicitWidth + 18); height: 34; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 11; contentItem: Text { text: parent.text; color: parent.checked ? colors.accent : colors.muted; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight } background: Rectangle { radius: 7; color: parent.checked ? colors.accentSoft : "transparent" } } }
        }
        Rectangle { width: parent.width; height: 1; color: colors.border }
        Rectangle { width: parent.width; height: parent.height - 110; radius: 10; color: colors.surface; border.color: colors.border
            Column { anchors.centerIn: parent; spacing: 9
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: page.tabNames[page.currentTab]; color: colors.text; font.bold: true; font.pixelSize: 17 }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: page.hasActiveWorld ? qsTr("This editor is ready for data integration.") : qsTr("Open a world to inspect its global data."); color: colors.muted; font.pixelSize: 12 }
            }
        }
    }
}

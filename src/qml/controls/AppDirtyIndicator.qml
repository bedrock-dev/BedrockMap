import BedrockMap 1.0
import QtQuick

Rectangle {
    id: root
    property bool dirty: false
    readonly property string description: qsTr("Unsaved changes")

    visible: dirty
    width: Theme.dirtyIndicatorSize
    height: width
    radius: width / 2
    color: Theme.warning
    border.width: 2
    border.color: Theme.surface
    Accessible.role: Accessible.StaticText
    Accessible.name: description
}

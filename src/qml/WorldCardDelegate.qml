import BedrockMap 1.0
import QtQuick

Item {
    id: root
    required property string name
    required property string version
    required property string time
    required property string size
    required property string source
    required property string path
    required property var thumbnail
    required property bool xorEncrypted
    property bool gridMode: false
    signal openRequested(string path, string title)

    WorldCard {
        anchors.fill: parent
        anchors.margins: root.gridMode ? (Theme.compact ? 3 : 4) : 0
        gridMode: root.gridMode
        title: root.name
        versionText: root.version
        thumbnailSource: root.thumbnail || ""
        tag: root.time
        sizeText: root.size
        sourceText: root.source
        pathText: root.path
        xorMarked: root.xorEncrypted
        onOpenRequested: (worldPath, worldTitle) => root.openRequested(worldPath, worldTitle)
    }
}

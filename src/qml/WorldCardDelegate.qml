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
    required property bool appInternal
    property bool gridMode: false
    signal openRequested(string path, string title)
    signal deleteRequested(string path, string title)
    signal exportRequested(string path, string title)
    signal exportAndOpenRequested(string path, string title)

    WorldCard {
        anchors.fill: parent
        anchors.margins: root.gridMode ? Theme.gapSmall : 0
        gridMode: root.gridMode
        title: root.name
        versionText: root.version
        thumbnailSource: root.thumbnail || ""
        tag: root.time
        sizeText: root.size
        sourceText: root.source
        pathText: root.path
        xorMarked: root.xorEncrypted
        appInternal: root.appInternal
        onOpenRequested: (worldPath, worldTitle) => root.openRequested(worldPath, worldTitle)
        onDeleteRequested: (worldPath, worldTitle) => root.deleteRequested(worldPath, worldTitle)
        onExportRequested: (worldPath, worldTitle) => root.exportRequested(worldPath, worldTitle)
        onExportAndOpenRequested: (worldPath, worldTitle) => root.exportAndOpenRequested(worldPath,
                                                                                       worldTitle)
    }
}

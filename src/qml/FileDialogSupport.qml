pragma Singleton

import QtQuick

QtObject {
    readonly property string allFilesFilter: qsTr("All files (*)")
    readonly property var binaryFilters: [qsTr("Binary files (*.bin)"), allFilesFilter]
    readonly property var glbFilters: [qsTr("GLB models (*.glb)")]
    readonly property var imageFilters: [qsTr("Images (*.png *.jpg *.jpeg *.bmp *.webp)"), allFilesFilter]
    readonly property var logFilters: [qsTr("Log files (*.log *.txt)")]
    readonly property var mcstructureFilters: [qsTr("Minecraft structures (*.mcstructure)")]
    readonly property var mcworldFilters: [qsTr("Minecraft worlds (*.mcworld)")]
    readonly property var minecraftFileFilters: [qsTr(
            "Minecraft files (*.mcworld *.nbt *.nbts *.mcstructure *.palette)"), allFilesFilter]
    readonly property var nbtFilters: [qsTr("NBT files (*.nbt *.nbts *.mcstructure *.palette)"), allFilesFilter]
    readonly property var nbtRecordFilters: [qsTr("NBT files (*.nbt *.nbts)"), allFilesFilter]
    readonly property var pngFilters: [qsTr("PNG images (*.png)")]
    readonly property var regionFilters: [qsTr("BedrockMap regions (*.bchks)")]

    function sanitizedFileName(name, fallback) {
        const sanitized = String(name || "").replace(/[<>:"/\\|?*\u0000-\u001f]/g, "_").replace(/[. ]+$/, "")
        return sanitized.length > 0 ? sanitized : String(fallback || "file")
    }

    function selectedUrl(dialog, fileName) {
        const folder = dialog && dialog.currentFolder ? dialog.currentFolder.toString().replace(/\/+$/, "") : ""
        const name = String(fileName || "")
        return folder.length > 0 ? folder + "/" + encodeURIComponent(name) : name
    }

    function selectedUrlWithSuffix(dialog, fileName, suffix) {
        let name = String(fileName || "")
        const normalizedSuffix = String(suffix || "")
        if (normalizedSuffix.length > 0 && !name.toLowerCase().endsWith("." + normalizedSuffix.toLowerCase()))
            name += "." + normalizedSuffix
        return selectedUrl(dialog, name)
    }
}

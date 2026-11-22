pragma Singleton

import QtQuick

QtObject {
    // User-visible descriptions intentionally reuse the existing ShortcutsPage
    // translation context while the command itself is implemented by each page.
    readonly property var app: [
        {
            command: "openWorld",
            sequence: "Ctrl+O",
            description: qsTranslate("ShortcutsPage", "Open world")
        },
        {
            command: "openFile",
            sequence: "Ctrl+Shift+O",
            description: qsTranslate("ShortcutsPage", "Open file")
        },
        {
            command: "newNbtDocument",
            sequence: "Ctrl+N",
            description: qsTranslate("ShortcutsPage", "New NBT document")
        },
        {
            command: "saveActiveSession",
            sequence: "Ctrl+S",
            description: qsTranslate("ShortcutsPage", "Save active document")
        },
        {
            command: "closeActiveSession",
            sequence: "Ctrl+W",
            description: qsTranslate("ShortcutsPage", "Close active session")
        },
        {
            command: "quitApplication",
            sequence: "Ctrl+Q",
            description: qsTranslate("ShortcutsPage", "Exit BedrockMap")
        },
        {
            command: "openSettings",
            sequence: "Ctrl+,",
            description: qsTranslate("ShortcutsPage", "Open settings")
        }
    ]
    readonly property var map: [
        {
            command: "toggleGrid",
            sequence: "Alt+G",
            description: qsTranslate("ShortcutsPage", "Toggle grid")
        },
        {
            command: "toggleCoordinates",
            sequence: "Alt+C",
            description: qsTranslate("ShortcutsPage", "Toggle coordinates")
        },
        {
            command: "dimensionOverworld",
            sequence: "Alt+1",
            description: qsTranslate("ShortcutsPage", "Overworld")
        },
        {
            command: "dimensionNether",
            sequence: "Alt+2",
            description: qsTranslate("ShortcutsPage", "Nether")
        },
        {
            command: "dimensionEnd",
            sequence: "Alt+3",
            description: qsTranslate("ShortcutsPage", "The End")
        },
        {
            command: "dimensionCustom",
            sequence: "Alt+4",
            description: qsTranslate("ShortcutsPage", "Custom dimension")
        },
        {
            command: "layerTerrain",
            sequence: "Alt+T",
            description: qsTranslate("ShortcutsPage", "Terrain layer")
        },
        {
            command: "layerBiome",
            sequence: "Alt+B",
            description: qsTranslate("ShortcutsPage", "Biome layer")
        },
        {
            command: "toggleSlime",
            sequence: "Alt+S",
            description: qsTranslate("ShortcutsPage", "Toggle slime chunks")
        },
        {
            command: "toggleEntities",
            sequence: "Alt+A",
            description: qsTranslate("ShortcutsPage", "Toggle entities")
        },
        {
            command: "toggleVillages",
            sequence: "Alt+V",
            description: qsTranslate("ShortcutsPage", "Toggle villages")
        },
        {
            command: "toggleHsa",
            sequence: "Alt+H",
            description: qsTranslate("ShortcutsPage", "Toggle HSA")
        },
        {
            command: "toggleTransparentVoid",
            sequence: "Alt+0",
            description: qsTranslate("ShortcutsPage", "Toggle transparent void")
        },
        {
            command: "toggleFilters",
            sequence: "Alt+F",
            description: qsTranslate("ShortcutsPage", "Toggle map filters")
        },
        {
            command: "toggleDebug",
            sequence: "Alt+D",
            description: qsTranslate("ShortcutsPage", "Toggle debug panels")
        }
    ]
    readonly property var chunk: [
        {
            command: "selectionReplace",
            sequence: "Shift+R",
            description: qsTranslate("ShortcutsPage", "Replace selection mode")
        },
        {
            command: "selectionAdd",
            sequence: "Shift+A",
            description: qsTranslate("ShortcutsPage", "Add selection mode")
        },
        {
            command: "selectionSubtract",
            sequence: "Shift+S",
            description: qsTranslate("ShortcutsPage", "Subtract selection mode")
        },
        {
            command: "clearSelection",
            sequence: "Shift+C",
            description: qsTranslate("ShortcutsPage", "Clear selection")
        },
        {
            command: "copySelection",
            sequence: "Ctrl+C",
            description: qsTranslate("ShortcutsPage", "Copy selected chunks"),
            requiresSelection: true
        },
        {
            command: "pasteSelection",
            sequence: "Ctrl+V",
            description: qsTranslate("ShortcutsPage", "Paste chunks")
        },
        {
            command: "exportSelection",
            sequence: "Ctrl+E",
            description: qsTranslate("ShortcutsPage", "Export selected chunks"),
            requiresSelection: true
        },
        {
            command: "importSelection",
            sequence: "Ctrl+I",
            description: qsTranslate("ShortcutsPage", "Import chunks")
        },
        {
            command: "deleteSelection",
            sequence: "Ctrl+D",
            description: qsTranslate("ShortcutsPage", "Delete selected chunks"),
            requiresSelection: true
        },
        {
            command: "clearChunks",
            sequence: "Ctrl+K",
            description: qsTranslate("ShortcutsPage", "Clear selected chunks"),
            requiresSelection: true
        },
        {
            command: "editBiome",
            sequence: "Ctrl+B",
            description: qsTranslate("ShortcutsPage", "Edit selection biome"),
            requiresSelection: true
        },
        {
            command: "screenshot",
            sequence: "Ctrl+T",
            description: qsTranslate("ShortcutsPage", "Save map screenshot")
        },
        {
            command: "openVoxel",
            sequence: "Ctrl+H",
            description: qsTranslate("ShortcutsPage", "Open 3D view")
        },
        {
            command: "focusCoordinates",
            sequence: "Ctrl+G",
            description: qsTranslate("ShortcutsPage", "Go to coordinates")
        }
    ]
    readonly property var voxel: [
        {
            command: "resetCamera",
            sequence: "R",
            description: qsTranslate("ShortcutsPage", "Reset camera")
        },
        {
            command: "faceFront",
            sequence: "F",
            description: qsTranslate("ShortcutsPage", "Face front")
        },
        {
            command: "toggleOrthographic",
            sequence: "O",
            description: qsTranslate("ShortcutsPage", "Toggle orthographic projection")
        },
        {
            command: "toggleAxes",
            sequence: "A",
            description: qsTranslate("ShortcutsPage", "Toggle axes")
        },
        {
            command: "toggleRotationLock",
            sequence: "L",
            description: qsTranslate("ShortcutsPage", "Toggle rotation lock")
        },
        {
            command: "toggleSelection",
            sequence: "S",
            description: qsTranslate("ShortcutsPage", "Toggle selection")
        },
        {
            command: "toggleSelectionMove",
            sequence: "M",
            description: qsTranslate("ShortcutsPage", "Toggle selection move mode"),
            requiresSelection: true
        },
        {
            command: "rotateLeft",
            sequence: "Left",
            description: qsTranslate("VoxelViewportOverlay", "Rotate left")
        },
        {
            command: "rotateRight",
            sequence: "Right",
            description: qsTranslate("VoxelViewportOverlay", "Rotate right")
        },
        {
            command: "rotateUp",
            sequence: "Up",
            description: qsTranslate("VoxelViewportOverlay", "Rotate up")
        },
        {
            command: "rotateDown",
            sequence: "Down",
            description: qsTranslate("VoxelViewportOverlay", "Rotate down")
        }
    ]

    function entries(group) {
        if (group === "app")
            return app
        if (group === "map")
            return map
        if (group === "chunk")
            return chunk
        if (group === "voxel")
            return voxel
        return []
    }

    function entry(group, command) {
        const definitions = entries(group)
        for (let i = 0; i < definitions.length; ++i)
            if (definitions[i].command === command)
                return definitions[i]
        return null
    }

    function sequence(group, command) {
        const definition = entry(group, command)
        return definition ? definition.sequence : ""
    }
}

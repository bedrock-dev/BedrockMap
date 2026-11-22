pragma Singleton

import QtQuick

QtObject {
    readonly property var definitions: ({
                                            world: {
                                                iconName: "map",
                                                untitledTitle: qsTr("Untitled world")
                                            },
                                            nbt: {
                                                iconName: "nbt_editor",
                                                untitledTitle: qsTr("Untitled NBT document")
                                            },
                                            mcstructure: {
                                                iconName: "structure",
                                                untitledTitle: qsTr("Untitled structure")
                                            }
                                        })

    function definition(kind) {
        return definitions[String(kind || "")] || null
    }

    function iconName(kind) {
        const entry = definition(kind)
        return entry ? entry.iconName : "map"
    }

    function untitledTitle(kind) {
        const entry = definition(kind)
        return entry ? entry.untitledTitle : qsTr("Untitled")
    }
}

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "controls"

Item {
    id: page
    property int category: 0
    readonly property bool compact: Theme.compact
    readonly property var openSourceCredits: [
        {
            name: "Qt",
            license: "LGPL-3.0 / GPL-2.0 / GPL-3.0",
            url: "https://www.qt.io/",
            licenseUrl: "https://doc.qt.io/qt-6/licensing.html"
        },
        {
            name: "bedrock-level",
            license: "AGPL-3.0",
            url: "https://github.com/bedrock-dev/bedrock-level",
            licenseUrl: "https://github.com/bedrock-dev/bedrock-level/blob/main/LICENSE"
        },
        {
            name: "JSON for Modern C++",
            license: "MIT",
            url: "https://github.com/nlohmann/json",
            licenseUrl: "https://opensource.org/license/mit"
        },
        {
            name: "LevelDB",
            license: "BSD-3-Clause",
            url: "https://github.com/google/leveldb",
            licenseUrl: "https://github.com/google/leveldb/blob/main/LICENSE"
        },
        {
            name: "leveldb-mcne",
            license: "MIT",
            url: "https://github.com/HTMonkeyG/leveldb-mcne",
            licenseUrl: "https://opensource.org/license/mit"
        },
        {
            name: "loguru",
            license: qsTr("Public domain"),
            url: "https://github.com/emilk/loguru",
            licenseUrl: "https://github.com/emilk/loguru#license"
        },
        {
            name: "magic_enum",
            license: "MIT",
            url: "https://github.com/Neargye/magic_enum",
            licenseUrl: "https://opensource.org/license/mit"
        },
        {
            name: "stb_image_write",
            license: "MIT / Unlicense",
            url: "https://github.com/nothings/stb",
            licenseUrl: "https://github.com/nothings/stb/blob/master/LICENSE"
        }
    ]
    readonly property var categoryNames: [qsTr("Interface"), qsTr("Map"), qsTr("Cache"), qsTr("Rendering"), qsTr("Misc"),
        qsTranslate("AboutDialog", "aboutDialog.title.about")]
    readonly property var zoomScaleLevels: [2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096]
    readonly property var renderBackendValues: Qt.platform.os === "android" ? ["automatic", "opengl", "vulkan"] :
                                                                              Qt.platform.os === "windows"
                                                                              ? ["automatic", "d3d11", "opengl",
                                                                                 "vulkan"] : ["automatic", "opengl",
                                                                                              "vulkan"]
    readonly property var renderBackendNames: Qt.platform.os === "android" ? [qsTr("Automatic"), qsTr("OpenGL ES"), qsTr(
                                                                                  "Vulkan")] : Qt.platform.os
                                                                             === "windows" ? [qsTr("Automatic"), qsTr(
                                                                                                  "Direct3D 11"), qsTr(
                                                                                                  "OpenGL"), qsTr(
                                                                                                  "Vulkan")] : [qsTr(
                                                                                                                    "Automatic"),
                                                                                                                qsTr("OpenGL"),
                                                                                                                qsTr("Vulkan")]

    FolderDialog {
        id: worldFolderDialog
        title: qsTr("Add world folder")
        onAccepted: {
            App.settings.addCustomWorldPath(String(selectedFolder))
            App.settings.saveWorldSources()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }
    ColumnLayout {
        id: shell
        anchors.fill: parent
        anchors.margins: Theme.pageGutter
        spacing: Theme.gapMedium

        AppSegmentedControl {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.touch ? Theme.segmentedHeight : 40
            options: page.categoryNames
            currentIndex: page.category
            onActivated: index => page.category = index
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical: ScrollBar {
                id: scrollBar
                background: Rectangle {
                    color: "transparent"
                }
                contentItem: Rectangle {
                    implicitWidth: 6
                    radius: Theme.radiusSmall
                    color: Theme.border
                    opacity: scrollBar.pressed || scrollBar.hovered ? 1.0 : 0.6
                }
            }
            ColumnLayout {
                width: scroll.availableWidth
                spacing: Theme.gapLarge
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gapMedium
                    Label {
                        Layout.fillWidth: true
                        text: page.categoryNames[page.category]
                        color: Theme.text
                        font.pixelSize: Theme.fontHeading
                        font.bold: true
                    }
                    AppButton {
                        visible: !page.compact
                        text: qsTr("Save")
                        primary: true
                        onClicked: App.settings.save()
                    }
                }
                ColumnLayout {
                    visible: page.category === 0
                    Layout.fillWidth: true
                    spacing: Theme.gapMedium
                    SettingGroup {
                        title: qsTr("Appearance")
                        SettingRow {
                            label: qsTr("Theme")
                            AppComboBox {
                                model: [qsTr("Light"), qsTr("Dark"), qsTr("System")]
                                currentIndex: App.settings.colorTheme === "light" ? 0 : App.settings.colorTheme
                                                                                    === "dark" ? 1 : 2
                                onActivated: {
                                    App.settings.colorTheme = ["light", "dark", "system"][currentIndex]
                                }
                            }
                        }
                        SettingRow {
                            label: qsTr("Language")
                            AppComboBox {
                                model: [qsTr("Chinese"), qsTr("English")]
                                currentIndex: App.settings.language === "en" ? 1 : 0
                                onActivated: App.settings.language = currentIndex === 1 ? "en" : "zh_CN"
                            }
                        }
                        // The icon theme stays in config.ini and is not offered here.
                        SettingRow {
                            label: qsTr("Font size")
                            AppSpinBox {
                                from: 6
                                to: 96
                                value: App.settings.fontSize
                                onValueModified: App.settings.fontSize = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Collapse sidebar")
                            AppSwitch {
                                checked: App.settings.collapseSidebarOnStartup
                                onToggled: App.settings.collapseSidebarOnStartup = checked
                                tooltip: qsTr("Automatically collapse the sidebar on startup")
                            }
                        }
                        SettingRow {
                            label: qsTr("Show debug panel button")
                            AppSwitch {
                                checked: App.settings.showMapDebugButton
                                onToggled: App.settings.showMapDebugButton = checked
                            }
                        }
                        // One colour drives the whole accent family: the fill, the
                        // tinted background and the text drawn on top of it.
                        SettingRow {
                            label: qsTr("Accent color")
                            ColumnLayout {
                                Layout.preferredWidth: Theme.ui(240)
                                spacing: Theme.gapSmall
                                Flow {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: implicitHeight
                                    spacing: Theme.gapSmall
                                    Repeater {
                                        model: ["#72d572", "#4c9aff", "#b57bff", "#ff9f43", "#ff6b6b", "#31c9c0"]
                                        delegate: Item {
                                            id: swatch
                                            required property string modelData
                                            width: Theme.touch ? Theme.controlHeight : 18
                                            height: width
                                            Rectangle {
                                                anchors.centerIn: parent
                                                width: Theme.touch ? Theme.iconLargeSize : 18
                                                height: width
                                                radius: width / 2
                                                color: swatch.modelData
                                                border.width: App.settings.accentColor === swatch.modelData ? 2 : 1
                                                border.color: App.settings.accentColor === swatch.modelData
                                                              ? Theme.text : (swatchMouse.pressed ? Theme.accent :
                                                                                                    Theme.border)
                                            }
                                            MouseArea {
                                                id: swatchMouse
                                                anchors.fill: parent
                                                onClicked: App.settings.accentColor = swatch.modelData
                                            }
                                        }
                                    }
                                }
                                RowLayout {
                                    spacing: Theme.gapSmall
                                    AppColorField {
                                        Layout.fillWidth: true
                                        value: App.settings.accentColor
                                        onEdited: color => App.settings.accentColor = color
                                    }
                                }
                            }
                        }
                    }
                }
                ColumnLayout {
                    visible: page.category === 1
                    Layout.fillWidth: true
                    spacing: Theme.gapMedium
                    SettingGroup {
                        title: qsTr("Zoom and appearance")
                        SettingRow {
                            label: qsTr("Minimum scale")
                            AppComboBox {
                                model: page.zoomScaleLevels.filter(scale => scale <= App.settings.maximumScaleLevel)
                                currentIndex: model.indexOf(App.settings.minimumScaleLevel)
                                onActivated: App.settings.minimumScaleLevel = model[currentIndex]
                            }
                        }
                        SettingRow {
                            label: qsTr("Maximum scale")
                            AppComboBox {
                                model: page.zoomScaleLevels.filter(scale => scale >= App.settings.minimumScaleLevel)
                                currentIndex: model.indexOf(App.settings.maximumScaleLevel)
                                onActivated: App.settings.maximumScaleLevel = model[currentIndex]
                            }
                        }
                        SettingRow {
                            label: qsTr("Pan inertia")
                            AppSlider {
                                Layout.preferredWidth: 220
                                from: 0
                                to: 1
                                value: App.settings.panInertia
                                onMoved: App.settings.panInertia = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Grid color")
                            AppColorField {
                                value: App.settings.gridLineColor
                                onEdited: color => App.settings.gridLineColor = color
                            }
                        }
                        SettingRow {
                            label: qsTr("Map selection color")
                            AppColorField {
                                value: App.settings.mapSelectionColor
                                onEdited: color => App.settings.mapSelectionColor = color
                            }
                        }
                        SettingRow {
                            label: qsTr("Void color")
                            AppColorField {
                                value: App.settings.voidMapColor
                                onEdited: color => App.settings.voidMapColor = color
                            }
                        }
                        SettingRow {
                            label: qsTr("Tile colors")
                            AppComboBox {
                                model: [qsTr("Dark"), qsTr("Light")]
                                currentIndex: App.settings.tileColorScheme === "light" ? 1 : 0
                                onActivated: App.settings.tileColorScheme = currentIndex === 1 ? "light" : "dark"
                            }
                        }
                    }
                    SettingGroup {
                        title: qsTr("Actors")
                        SettingRow {
                            label: qsTr("Actor style")
                            AppComboBox {
                                model: [qsTr("Individual"), qsTr("Consolidated")]
                                currentIndex: App.settings.actorRenderStyle
                                onActivated: App.settings.actorRenderStyle = currentIndex
                            }
                        }
                        SettingRow {
                            label: qsTr("Border width")
                            AppSpinBox {
                                from: 0
                                to: 32
                                value: App.settings.actorBorderWidth
                                onValueModified: App.settings.actorBorderWidth = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Border color")
                            AppColorField {
                                value: App.settings.actorBorderColor
                                onEdited: color => App.settings.actorBorderColor = color
                            }
                        }
                    }
                    SettingGroup {
                        title: qsTr("Chunk editor and voxel selection")
                        SettingRow {
                            label: qsTr("Editor color")
                            AppColorField {
                                value: App.settings.chunkEditorHighlightColor
                                onEdited: color => App.settings.chunkEditorHighlightColor = color
                            }
                        }
                        SettingRow {
                            label: qsTr("Editor width")
                            AppSpinBox {
                                from: 0
                                to: 64
                                value: App.settings.chunkEditorHighlightWidth
                                onValueModified: App.settings.chunkEditorHighlightWidth = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Voxel selection color")
                            AppColorField {
                                value: App.settings.voxelSelectionColor
                                onEdited: color => App.settings.voxelSelectionColor = color
                            }
                        }
                    }
                }
                ColumnLayout {
                    visible: page.category === 2
                    Layout.fillWidth: true
                    spacing: Theme.gapMedium
                    SettingGroup {
                        title: qsTr("Caches")
                        SettingRow {
                            label: qsTr("Region cache size")
                            AppSpinBox {
                                from: 0
                                to: 10000000
                                value: App.settings.regionCacheSize
                                onValueModified: App.settings.regionCacheSize = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Empty region cache size")
                            AppSpinBox {
                                from: 0
                                to: 10000000
                                value: App.settings.emptyRegionCacheSize
                                onValueModified: App.settings.emptyRegionCacheSize = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Background threads")
                            AppSpinBox {
                                from: 1
                                to: 128
                                value: App.settings.threadCount
                                onValueModified: App.settings.threadCount = value
                            }
                        }
                    }
                }
                ColumnLayout {
                    visible: page.category === 4
                    Layout.fillWidth: true
                    spacing: Theme.gapMedium
                    SettingGroup {
                        title: qsTr("World sources")
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gapSmall
                            AppButton {
                                compact: true
                                text: qsTr("Add folder")
                                visible: !Theme.touch
                                onClicked: worldFolderDialog.open()
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                        }
                        Repeater {
                            model: App.settings.worldSources
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: Theme.gapSmall
                                Text {
                                    text: qsTranslate("WorldSource", modelData.label)
                                    color: Theme.text
                                    font.pixelSize: Theme.fontCaption
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.path
                                    color: Theme.muted
                                    font.pixelSize: Theme.fontCaption
                                    elide: Text.ElideMiddle
                                }
                                Text {
                                    visible: !modelData.path
                                    text: qsTr("Not configured")
                                    color: Theme.warning
                                    font.pixelSize: Theme.fontCaption
                                }
                                AppIconButton {
                                    visible: modelData.removable
                                    iconName: "close"
                                    label: qsTr("Remove world folder")
                                    buttonSize: 28
                                    onClicked: {
                                        App.settings.removeCustomWorldPath(modelData.path)
                                        App.settings.saveWorldSources()
                                    }
                                }
                            }
                        }
                    }
                    SettingGroup {
                        title: qsTr("World data")
                        SettingRow {
                            label: qsTr("Load global data")
                            AppSwitch {
                                checked: App.settings.loadGlobalData
                                onToggled: App.settings.loadGlobalData = checked
                            }
                        }
                        SettingRow {
                            label: qsTr("Maximum global data entries")
                            AppSpinBox {
                                enabled: App.settings.loadGlobalData
                                from: 0
                                to: 10000000
                                value: App.settings.maxGlobalDataLoadCount
                                onValueModified: App.settings.maxGlobalDataLoadCount = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Preload chunk coordinates")
                            AppSwitch {
                                checked: App.settings.preloadAllChunkCoords
                                onToggled: App.settings.preloadAllChunkCoords = checked
                            }
                        }
                        SettingRow {
                            label: qsTr("Terrain overview")
                            AppSwitch {
                                enabled: App.settings.preloadAllChunkCoords
                                checked: enabled && App.settings.chunkOverviewEnabled
                                onToggled: App.settings.chunkOverviewEnabled = checked
                            }
                        }
                    }
                    SettingGroup {
                        title: qsTr("LevelDB XOR")
                        SettingRow {
                            label: qsTr("Mode")
                            AppComboBox {
                                model: [qsTr("Disabled"), qsTr("Enabled"), qsTr("Auto detect")]
                                currentIndex: App.settings.leveldbXorMode
                                onActivated: App.settings.leveldbXorMode = currentIndex
                            }
                        }
                        SettingRow {
                            label: qsTr("Secret key")
                            AppTextField {
                                text: App.settings.leveldbXorKey
                                onEditingFinished: App.settings.leveldbXorKey = text
                            }
                        }
                    }
                    SettingGroup {
                        title: qsTr("Startup")
                        SettingRow {
                            label: qsTr("Scan LeviLauncher paths")
                            AppSwitch {
                                checked: App.settings.scanLeviPath
                                onToggled: App.settings.scanLeviPath = checked
                            }
                        }
                    }
                }
                ColumnLayout {
                    visible: page.category === 3
                    Layout.fillWidth: true
                    spacing: Theme.gapMedium
                    SettingGroup {
                        title: qsTr("Backend")
                        SettingRow {
                            label: qsTr("Graphics backend")
                            AppComboBox {
                                model: page.renderBackendNames
                                currentIndex: Math.max(0, page.renderBackendValues.indexOf(App.settings.renderBackend))
                                onActivated: App.settings.renderBackend = page.renderBackendValues[currentIndex]
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Graphics backend changes take effect after restarting the application.")
                            color: Theme.muted
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.WordWrap
                        }
                    }
                    SettingGroup {
                        title: qsTr("Shadows and occlusion")
                        SettingRow {
                            label: qsTr("Shadow strength")
                            AppSlider {
                                Layout.preferredWidth: 220
                                from: 0
                                to: 1
                                value: App.settings.gpuShadowStrength
                                onMoved: App.settings.gpuShadowStrength = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Shadow precision")
                            AppComboBox {
                                model: [4, 8, 16, 32]
                                currentIndex: [4, 8, 16, 32].indexOf(App.settings.gpuShadowStep)
                                onActivated: App.settings.gpuShadowStep = model[currentIndex]
                            }
                        }
                        SettingRow {
                            label: qsTr("Ambient occlusion")
                            AppSlider {
                                Layout.preferredWidth: 220
                                from: 0
                                to: 1
                                value: App.settings.gpuAoStrength
                                onMoved: App.settings.gpuAoStrength = value
                            }
                        }
                    }
                    SettingGroup {
                        title: qsTr("Terrain appearance")
                        SettingRow {
                            label: qsTr("Saturation")
                            AppSlider {
                                Layout.preferredWidth: 220
                                from: 0
                                to: 2
                                value: App.settings.gpuSaturation
                                onMoved: App.settings.gpuSaturation = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Brightness")
                            AppSlider {
                                Layout.preferredWidth: 220
                                from: 0
                                to: 2
                                value: App.settings.gpuBrightness
                                onMoved: App.settings.gpuBrightness = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Bevel strength")
                            AppSlider {
                                Layout.preferredWidth: 220
                                from: 0
                                to: 1
                                value: App.settings.gpuBevelStrength
                                onMoved: App.settings.gpuBevelStrength = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Bevel width")
                            AppSlider {
                                Layout.preferredWidth: 220
                                from: 0.25
                                to: 2
                                value: App.settings.gpuBevelWidth
                                onMoved: App.settings.gpuBevelWidth = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Biome blend blocks")
                            AppSlider {
                                Layout.preferredWidth: 220
                                from: 0
                                to: 16
                                value: App.settings.gpuBiomeBlendBlocks
                                onMoved: App.settings.gpuBiomeBlendBlocks = value
                            }
                        }
                    }
                    SettingGroup {
                        title: qsTr("Grass height shading")
                        SettingRow {
                            label: qsTr("Enabled")
                            AppSwitch {
                                checked: App.settings.gpuGrassHeightEnabled
                                onToggled: App.settings.gpuGrassHeightEnabled = checked
                            }
                        }
                        SettingRow {
                            label: qsTr("Base height")
                            AppSpinBox {
                                enabled: App.settings.gpuGrassHeightEnabled
                                from: -128
                                to: 512
                                value: App.settings.gpuGrassHeightBase
                                onValueModified: App.settings.gpuGrassHeightBase = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Range")
                            AppSpinBox {
                                enabled: App.settings.gpuGrassHeightEnabled
                                from: 1
                                to: 512
                                value: App.settings.gpuGrassHeightRange
                                onValueModified: App.settings.gpuGrassHeightRange = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Marker color")
                            AppColorField {
                                enabled: App.settings.gpuGrassHeightEnabled
                                value: App.settings.gpuGrassHeightColor
                                onEdited: color => App.settings.gpuGrassHeightColor = color
                            }
                        }
                    }
                    SettingGroup {
                        title: qsTr("Performance")
                        SettingRow {
                            label: qsTr("High-detail map")
                            AppSwitch {
                                checked: App.settings.gpuHighDetailMap
                                onToggled: App.settings.gpuHighDetailMap = checked
                                tooltip: qsTr(
                                             "Keep full-resolution terrain and shading further out when zooming. Uses up to 256 MiB more GPU memory and builds more tiles.")
                            }
                        }
                        SettingRow {
                            label: qsTr("Tile snapshot cache (MiB)")
                            AppSpinBox {
                                from: 8
                                to: 512
                                value: App.settings.gpuTileSnapshotCacheSizeMB
                                onValueModified: App.settings.gpuTileSnapshotCacheSizeMB = value
                            }
                        }
                    }
                }
                ColumnLayout {
                    visible: page.category === 5
                    Layout.fillWidth: true
                    Layout.maximumWidth: Theme.ui(820)
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.gapMedium
                    SettingGroup {
                        title: qsTr("Application information")
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gapLarge
                            Rectangle {
                                Layout.preferredWidth: 76
                                Layout.preferredHeight: 76
                                radius: Theme.radiusCard
                                color: Theme.accentSoft
                                Image {
                                    anchors.centerIn: parent
                                    width: 58
                                    height: 58
                                    source: "qrc:/bedrockmap/icons/app.svg"
                                    sourceSize.width: 116
                                    sourceSize.height: 116
                                    smooth: true
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.gapSmall
                                Text {
                                    Layout.fillWidth: true
                                    text: Qt.application.name
                                    color: Theme.text
                                    font.pixelSize: Theme.fontHeading
                                    font.bold: true
                                    wrapMode: Text.Wrap
                                }
                                Text {
                                    text: Qt.application.version
                                    color: Theme.accent
                                    font.pixelSize: Theme.fontSubtitle
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTranslate("AboutDialog", "aboutDialog.copyright")
                                    color: Theme.muted
                                    font.pixelSize: Theme.fontBody
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTranslate("AboutDialog", "aboutDialog.description")
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            textFormat: Text.StyledText
                            text: qsTranslate("AboutDialog", "aboutDialog.developer").arg(
                                      "<a href=\"https://github.com/hhhxiao\">hhhxiao</a>")
                            color: Theme.muted
                            linkColor: Theme.accent
                            font.pixelSize: Theme.fontBody
                            wrapMode: Text.WordWrap
                            onLinkActivated: link => Qt.openUrlExternally(link)
                        }
                        Flow {
                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitHeight
                            spacing: Theme.gapSmall
                            AppButton {
                                text: qsTranslate("AboutDialog", "aboutDialog.github")
                                onClicked: Qt.openUrlExternally("https://github.com/bedrock-dev/BedrockMap")
                            }
                            AppButton {
                                text: qsTranslate("AboutDialog", "aboutDialog.joinQQGroup")
                                onClicked: Qt.openUrlExternally("https://qm.qq.com/q/3KJZZCkDbW")
                            }
                        }
                    }
                    SettingGroup {
                        title: qsTr("Updates")
                        GridLayout {
                            Layout.fillWidth: true
                            columns: page.compact ? 1 : 2
                            columnSpacing: Theme.gapLarge
                            rowSpacing: Theme.gapSmall
                            AppButton {
                                text: App.checkingForUpdates ? qsTr("Checking...") : qsTr("Check for updates")
                                enabled: !App.checkingForUpdates
                                onClicked: App.checkForUpdates(true)
                            }
                            AppCheckBox {
                                Layout.fillWidth: true
                                text: qsTr("Don't check for updates on startup")
                                checked: !App.settings.checkUpdate
                                onToggled: App.settings.checkUpdate = !checked
                            }
                        }
                    }
                    SettingGroup {
                        title: qsTranslate("AboutDialog", "aboutDialog.otherContributors")
                        Text {
                            Layout.fillWidth: true
                            textFormat: Text.StyledText
                            text: "<a href=\"https://space.bilibili.com/382492603\">INTZOT</a>  ·  "
                                  + "<a href=\"https://github.com/OEOTYAN\">OEOYAN</a>"
                            color: Theme.muted
                            linkColor: Theme.accent
                            font.pixelSize: Theme.fontBody
                            wrapMode: Text.Wrap
                            onLinkActivated: link => Qt.openUrlExternally(link)
                        }
                    }
                    SettingGroup {
                        title: qsTr("Open-source credits")
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Thanks to the following open-source projects.")
                            color: Theme.muted
                            font.pixelSize: Theme.fontBody
                            wrapMode: Text.WordWrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gapMedium
                            Text {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 0
                                text: qsTr("Component")
                                color: Theme.muted
                                font.pixelSize: Theme.fontCaption
                            }
                            Text {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 0
                                text: qsTr("License")
                                color: Theme.muted
                                font.pixelSize: Theme.fontCaption
                            }
                        }
                        Repeater {
                            model: page.openSourceCredits
                            delegate: RowLayout {
                                id: creditRow
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.minimumHeight: Theme.controlHeight
                                spacing: Theme.gapMedium
                                Text {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 0
                                    Layout.minimumWidth: 0
                                    Layout.alignment: Qt.AlignTop
                                    text: creditRow.modelData.url ? "<a href=\"" + creditRow.modelData.url + "\">"
                                                                  + creditRow.modelData.name + "</a>" : creditRow.modelData.name
                                    textFormat: Text.StyledText
                                    color: Theme.text
                                    linkColor: Theme.accent
                                    font.pixelSize: Theme.fontBody
                                    wrapMode: Text.Wrap
                                    onLinkActivated: link => Qt.openUrlExternally(link)
                                }
                                Text {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 0
                                    Layout.minimumWidth: 0
                                    Layout.alignment: Qt.AlignTop
                                    text: creditRow.modelData.licenseUrl ? "<a href=\"" + creditRow.modelData.licenseUrl
                                                                         + "\">" + creditRow.modelData.license + "</a>" :
                                                                         creditRow.modelData.license
                                    textFormat: Text.StyledText
                                    color: Theme.text
                                    linkColor: Theme.accent
                                    font.pixelSize: Theme.fontBody
                                    wrapMode: Text.Wrap
                                    onLinkActivated: link => Qt.openUrlExternally(link)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    component SettingGroup: AppCard {
        property string title
        default property alias content: body.data
        Layout.fillWidth: true
        // Bounded by construction: a group must never inflate the scroll content,
        // or the trailing controls end up outside the visible viewport.
        Layout.preferredWidth: 0
        implicitHeight: titleLabel.implicitHeight + body.implicitHeight + Theme.cardPadding * 2 + Theme.gapSmall
        Label {
            id: titleLabel
            x: Theme.cardPadding
            y: Theme.cardPadding
            text: title
            color: Theme.text
            font.bold: true
            font.pixelSize: Theme.fontLabel
        }
        ColumnLayout {
            id: body
            anchors.left: parent.left
            anchors.leftMargin: Theme.cardPadding
            anchors.right: parent.right
            anchors.rightMargin: Theme.cardPadding
            anchors.top: titleLabel.bottom
            anchors.topMargin: Theme.gapSmall
            spacing: Theme.gapSmall
        }
    }
    component SettingRow: RowLayout {
        property string label
        Layout.fillWidth: true
        Layout.preferredWidth: 0
        implicitHeight: Theme.settingRowHeight
        Label {
            text: label
            color: Theme.text
            font.pixelSize: Theme.fontSubtitle
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        // The second child supplied by each row occupies the trailing column.
    }

}

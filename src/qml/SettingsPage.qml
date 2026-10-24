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
    readonly property var categoryNames: [qsTr("Interface"), qsTr("Map"), qsTr("Cache"), qsTr("Rendering"), qsTr(
            "Misc"), qsTranslate("AboutDialog", "aboutDialog.title.about")]
    readonly property var zoomScaleLevels: [2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096]
    readonly property var renderBackendValues: Qt.platform.os === "android"
                                                ? ["automatic", "opengl", "vulkan"]
                                                : Qt.platform.os === "windows"
                                                  ? ["automatic", "d3d11", "opengl", "vulkan"]
                                                  : ["automatic", "opengl", "vulkan"]
    readonly property var renderBackendNames: Qt.platform.os === "android"
                                               ? [qsTr("Automatic"), qsTr("OpenGL ES"), qsTr("Vulkan")]
                                               : Qt.platform.os === "windows"
                                                 ? [qsTr("Automatic"), qsTr("Direct3D 11"), qsTr("OpenGL"), qsTr("Vulkan")]
                                                 : [qsTr("Automatic"), qsTr("OpenGL"), qsTr("Vulkan")]

    FolderDialog {
        id: worldFolderDialog
        title: qsTr("Add world folder")
        onAccepted: {
            Settings.addCustomWorldPath(String(selectedFolder))
            Settings.saveWorldSources()
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
            Layout.preferredHeight: Theme.touch ? Theme.controlHeight + 6 : 40
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
                width: Math.max(scroll.availableWidth, 320)
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
                        onClicked: Settings.save()
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
                                currentIndex: Settings.colorTheme === "light" ? 0 : Settings.colorTheme
                                                                                     === "dark" ? 1 : 2
                                onActivated: {
                                    Settings.colorTheme = ["light", "dark", "system"][currentIndex]
                                }
                            }
                        }
                        SettingRow {
                            label: qsTr("Language")
                            AppComboBox {
                                model: [qsTr("Chinese"), qsTr("English")]
                                currentIndex: Settings.language === "en" ? 1 : 0
                                onActivated: Settings.language = currentIndex === 1 ? "en" : "zh_CN"
                            }
                        }
                        // The icon theme stays in config.ini and is not offered here.
                        SettingRow {
                            label: qsTr("Font size")
                            AppSpinBox {
                                from: 6
                                to: 96
                                value: Settings.fontSize
                                onValueModified: Settings.fontSize = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Collapse sidebar")
                            AppSwitch {
                                checked: Settings.collapseSidebarOnStartup
                                onToggled: Settings.collapseSidebarOnStartup = checked
                                tooltip: qsTr("Automatically collapse the sidebar on startup")
                            }
                        }
                        // One colour drives the whole accent family: the fill, the
                        // tinted background and the text drawn on top of it.
                        SettingRow {
                            label: qsTr("Accent color")
                            ColumnLayout {
                                Layout.preferredWidth: 240
                                spacing: Theme.gapSmall
                                RowLayout {
                                    spacing: Theme.gapSmall
                                    Repeater {
                                        model: ["#72d572", "#4c9aff", "#b57bff", "#ff9f43", "#ff6b6b", "#31c9c0"]
                                        delegate: Rectangle {
                                            required property string modelData
                                            width: Theme.touch ? 28 : 18
                                            height: width
                                            radius: width / 2
                                            color: modelData
                                            border.width: Settings.accentColor === modelData ? 2 : 1
                                            border.color: Settings.accentColor === modelData ? Theme.text :
                                                                                                    (swatchMouse.pressed
                                                                                                     ? Theme.accent :
                                                                                                       Theme.border)
                                            MouseArea {
                                                id: swatchMouse
                                                anchors.fill: parent
                                                onClicked: Settings.accentColor = modelData
                                            }
                                        }
                                    }
                                }
                                RowLayout {
                                    spacing: Theme.gapSmall
                                    AppColorField {
                                        Layout.fillWidth: true
                                        value: Settings.accentColor
                                        onEdited: color => Settings.accentColor = color
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
                                model: page.zoomScaleLevels.filter(scale => scale <= Settings.maximumScaleLevel)
                                currentIndex: model.indexOf(Settings.minimumScaleLevel)
                                onActivated: Settings.minimumScaleLevel = model[currentIndex]
                            }
                        }
                        SettingRow {
                            label: qsTr("Maximum scale")
                            AppComboBox {
                                model: page.zoomScaleLevels.filter(scale => scale >= Settings.minimumScaleLevel)
                                currentIndex: model.indexOf(Settings.maximumScaleLevel)
                                onActivated: Settings.maximumScaleLevel = model[currentIndex]
                            }
                        }
                        SettingRow {
                            label: qsTr("Pan inertia")
                            AppSlider {
                                Layout.preferredWidth: 220
                                from: 0
                                to: 1
                                value: Settings.panInertia
                                onMoved: Settings.panInertia = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Grid color")
                            AppColorField {
                                value: Settings.gridLineColor
                                onEdited: color => Settings.gridLineColor = color
                            }
                        }
                        SettingRow {
                            label: qsTr("Map selection color")
                            AppColorField {
                                value: Settings.mapSelectionColor
                                onEdited: color => Settings.mapSelectionColor = color
                            }
                        }
                        SettingRow {
                            label: qsTr("Void color")
                            AppColorField {
                                value: Settings.voidMapColor
                                onEdited: color => Settings.voidMapColor = color
                            }
                        }
                        SettingRow {
                            label: qsTr("Tile colors")
                            AppComboBox {
                                model: [qsTr("Dark"), qsTr("Light")]
                                currentIndex: Settings.tileColorScheme === "light" ? 1 : 0
                                onActivated: Settings.tileColorScheme = currentIndex === 1 ? "light" : "dark"
                            }
                        }
                    }
                    SettingGroup {
                        title: qsTr("Actors")
                        SettingRow {
                            label: qsTr("Actor style")
                            AppComboBox {
                                model: [qsTr("Individual"), qsTr("Consolidated")]
                                currentIndex: Settings.actorRenderStyle
                                onActivated: Settings.actorRenderStyle = currentIndex
                            }
                        }
                        SettingRow {
                            label: qsTr("Border width")
                            AppSpinBox {
                                from: 0
                                to: 32
                                value: Settings.actorBorderWidth
                                onValueModified: Settings.actorBorderWidth = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Border color")
                            AppColorField {
                                value: Settings.actorBorderColor
                                onEdited: color => Settings.actorBorderColor = color
                            }
                        }
                    }
                    SettingGroup {
                        title: qsTr("Chunk editor and voxel selection")
                        SettingRow {
                            label: qsTr("Editor color")
                            AppColorField {
                                value: Settings.chunkEditorHighlightColor
                                onEdited: color => Settings.chunkEditorHighlightColor = color
                            }
                        }
                        SettingRow {
                            label: qsTr("Editor width")
                            AppSpinBox {
                                from: 0
                                to: 64
                                value: Settings.chunkEditorHighlightWidth
                                onValueModified: Settings.chunkEditorHighlightWidth = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Voxel selection color")
                            AppColorField {
                                value: Settings.voxelSelectionColor
                                onEdited: color => Settings.voxelSelectionColor = color
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
                                value: Settings.regionCacheSize
                                onValueModified: Settings.regionCacheSize = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Empty region cache size")
                            AppSpinBox {
                                from: 0
                                to: 10000000
                                value: Settings.emptyRegionCacheSize
                                onValueModified: Settings.emptyRegionCacheSize = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Height map cache size")
                            AppSpinBox {
                                from: 0
                                to: 10000000
                                value: Settings.heightMapCacheSize
                                onValueModified: Settings.heightMapCacheSize = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Background threads")
                            AppSpinBox {
                                from: 1
                                to: 128
                                value: Settings.threadCount
                                onValueModified: Settings.threadCount = value
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
                            model: Settings.worldSources
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: Theme.gapSmall
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.path
                                    color: Theme.muted
                                    font.pixelSize: Theme.fontCaption
                                    elide: Text.ElideMiddle
                                }
                                Text {
                                    visible: modelData.availability !== "Available"
                                    text: modelData.availability === "NeedsAllFiles" ? qsTr("Requires All files access") :
                                                                                        qsTr("Requires privileged access")
                                    color: Theme.warning
                                    font.pixelSize: Theme.fontCaption
                                }
                                AppIconButton {
                                    visible: modelData.removable
                                    iconName: "close"
                                    label: qsTr("Remove world folder")
                                    buttonSize: 28
                                    onClicked: {
                                        Settings.removeCustomWorldPath(modelData.path)
                                        Settings.saveWorldSources()
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
                                checked: Settings.loadGlobalData
                                onToggled: Settings.loadGlobalData = checked
                            }
                        }
                        SettingRow {
                            label: qsTr("Maximum global data entries")
                            AppSpinBox {
                                enabled: Settings.loadGlobalData
                                from: 0
                                to: 10000000
                                value: Settings.maxGlobalDataLoadCount
                                onValueModified: Settings.maxGlobalDataLoadCount = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Preload chunk coordinates")
                            AppSwitch {
                                checked: Settings.preloadAllChunkCoords
                                onToggled: Settings.preloadAllChunkCoords = checked
                            }
                        }
                        SettingRow {
                            label: qsTr("Terrain overview")
                            AppSwitch {
                                enabled: Settings.preloadAllChunkCoords
                                checked: enabled && Settings.chunkOverviewEnabled
                                onToggled: Settings.chunkOverviewEnabled = checked
                            }
                        }
                    }
                    SettingGroup {
                        title: qsTr("LevelDB XOR")
                        SettingRow {
                            label: qsTr("Mode")
                            AppComboBox {
                                model: [qsTr("Disabled"), qsTr("Enabled"), qsTr("Auto detect")]
                                currentIndex: Settings.leveldbXorMode
                                onActivated: Settings.leveldbXorMode = currentIndex
                            }
                        }
                        SettingRow {
                            label: qsTr("Key")
                            AppTextField {
                                text: Settings.leveldbXorKey
                                onEditingFinished: Settings.leveldbXorKey = text
                            }
                        }
                    }
                    SettingGroup {
                        title: qsTr("Startup")
                        SettingRow {
                            label: qsTr("Check for updates")
                            AppSwitch {
                                checked: Settings.checkUpdate
                                onToggled: Settings.checkUpdate = checked
                            }
                        }
                        SettingRow {
                            label: qsTr("Scan LeviLauncher paths")
                            AppSwitch {
                                checked: Settings.scanLeviPath
                                onToggled: Settings.scanLeviPath = checked
                            }
                        }
                    }
                }
                ColumnLayout {
                    visible: page.category === 3
                    Layout.fillWidth: true
                    spacing: Theme.gapMedium
                    SettingGroup {
                        title: qsTr("Renderer")
                        SettingRow {
                            label: qsTr("Graphics backend")
                            AppComboBox {
                                model: page.renderBackendNames
                                currentIndex: Math.max(0, page.renderBackendValues.indexOf(Settings.renderBackend))
                                onActivated: Settings.renderBackend = page.renderBackendValues[currentIndex]
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Graphics backend changes take effect after restarting the application.")
                            color: Theme.muted
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.WordWrap
                        }
                        SettingRow {
                            label: qsTr("Orthographic view")
                            AppSwitch {
                                checked: Settings.gpuOrthographicView
                                onToggled: Settings.gpuOrthographicView = checked
                            }
                        }
                        SettingRow {
                            label: qsTr("GPU shadow strength")
                            AppSlider {
                                Layout.preferredWidth: 220
                                from: 0
                                to: 1
                                value: Settings.gpuShadowStrength
                                onMoved: Settings.gpuShadowStrength = value
                            }
                        }
                        SettingRow {
                            label: qsTr("GPU ambient occlusion")
                            AppSlider {
                                Layout.preferredWidth: 220
                                from: 0
                                to: 1
                                value: Settings.gpuAoStrength
                                onMoved: Settings.gpuAoStrength = value
                            }
                        }
                        SettingRow {
                            label: qsTr("GPU bevel strength")
                            AppSlider {
                                Layout.preferredWidth: 220
                                from: 0
                                to: 1
                                value: Settings.gpuBevelStrength
                                onMoved: Settings.gpuBevelStrength = value
                            }
                        }
                        SettingRow {
                            label: qsTr("GPU bevel width")
                            AppSlider {
                                Layout.preferredWidth: 220
                                from: 0.25
                                to: 2
                                value: Settings.gpuBevelWidth
                                onMoved: Settings.gpuBevelWidth = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Saturation")
                            AppSlider {
                                Layout.preferredWidth: 220
                                from: 0
                                to: 2
                                value: Settings.gpuSaturation
                                onMoved: Settings.gpuSaturation = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Brightness")
                            AppSlider {
                                Layout.preferredWidth: 220
                                from: 0
                                to: 2
                                value: Settings.gpuBrightness
                                onMoved: Settings.gpuBrightness = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Biome blend blocks")
                            AppSlider {
                                Layout.preferredWidth: 220
                                from: 0
                                to: 16
                                value: Settings.gpuBiomeBlendBlocks
                                onMoved: Settings.gpuBiomeBlendBlocks = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Shadow precision")
                            AppComboBox {
                                model: [4, 8, 16, 32]
                                currentIndex: [4, 8, 16, 32].indexOf(Settings.gpuShadowStep)
                                onActivated: Settings.gpuShadowStep = model[currentIndex]
                            }
                        }
                        SettingRow {
                            label: qsTr("Tile snapshot cache (MiB)")
                            AppSpinBox {
                                from: 8
                                to: 512
                                value: Settings.gpuTileSnapshotCacheSizeMB
                                onValueModified: Settings.gpuTileSnapshotCacheSizeMB = value
                            }
                        }
                    }
                    SettingGroup {
                        title: qsTr("Grass height shading")
                        SettingRow {
                            label: qsTr("Enabled")
                            AppSwitch {
                                checked: Settings.gpuGrassHeightEnabled
                                onToggled: Settings.gpuGrassHeightEnabled = checked
                            }
                        }
                        SettingRow {
                            label: qsTr("Base height")
                            AppSpinBox {
                                enabled: Settings.gpuGrassHeightEnabled
                                from: -128
                                to: 512
                                value: Settings.gpuGrassHeightBase
                                onValueModified: Settings.gpuGrassHeightBase = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Range")
                            AppSpinBox {
                                enabled: Settings.gpuGrassHeightEnabled
                                from: 1
                                to: 512
                                value: Settings.gpuGrassHeightRange
                                onValueModified: Settings.gpuGrassHeightRange = value
                            }
                        }
                        SettingRow {
                            label: qsTr("Marker color")
                            AppColorField {
                                enabled: Settings.gpuGrassHeightEnabled
                                value: Settings.gpuGrassHeightColor
                                onEdited: color => Settings.gpuGrassHeightColor = color
                            }
                        }
                    }
                }
                ColumnLayout {
                    visible: page.category === 5
                    Layout.fillWidth: true
                    spacing: Theme.gapMedium
                    SettingGroup {
                        title: qsTranslate("AboutDialog", "aboutDialog.title.about")
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
                                    source: "qrc:/BedrockMap.svg"
                                    sourceSize.width: 116
                                    sourceSize.height: 116
                                    smooth: true
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.gapSmall
                                Text {
                                    text: Qt.application.name
                                    color: Theme.text
                                    font.pixelSize: Theme.fontHeading
                                    font.bold: true
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
                    }
                    SettingGroup {
                        title: qsTranslate("AboutDialog", "aboutDialog.links")
                        RowLayout {
                            Layout.fillWidth: true
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
                        Text {
                            Layout.fillWidth: true
                            text: qsTranslate("AboutDialog", "aboutDialog.otherContributors")
                            color: Theme.text
                            font.pixelSize: Theme.fontSubtitle
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            textFormat: Text.StyledText
                            text: "<a href=\"https://space.bilibili.com/382492603\">INTZOT</a>  ·  "
                                  + "<a href=\"https://github.com/OEOTYAN\">OEOYAN</a>"
                            color: Theme.muted
                            linkColor: Theme.accent
                            font.pixelSize: Theme.fontBody
                            onLinkActivated: link => Qt.openUrlExternally(link)
                        }
                    }
                    SettingGroup {
                        visible: !page.compact
                        title: qsTr("Button styles")
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gapMedium
                            AppButton {
                                text: qsTr("Primary")
                                primary: true
                            }
                            AppButton {
                                text: qsTr("Default")
                            }
                            AppButton {
                                text: qsTr("Active")
                                active: true
                            }
                            AppButton {
                                text: qsTr("Danger")
                                danger: true
                            }
                            AppButton {
                                text: qsTr("Ghost")
                                ghost: true
                                bordered: false
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

    Connections {
        target: Settings
        function onSaved() {
            Notifications.info(qsTr("Settings saved"))
        }
    }
}

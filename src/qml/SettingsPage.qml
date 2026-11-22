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
    readonly property var initialMapPositionValues: ["origin", "world_spawn", "player"]
    readonly property var initialMapPositionNames: [qsTr("World origin"), qsTr("World spawn"), qsTr("Local player")]
    readonly property var zoomScaleLevels: [2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096]
    readonly property var colorThemeValues: ["light", "dark", "black", "system"]
    readonly property var renderBackendValues: {
        if (Qt.platform.os === "android")
            return ["automatic", "opengl", "vulkan"]
        if (Qt.platform.os === "windows")
            return ["automatic", "d3d11", "opengl", "vulkan"]
        return ["automatic", "opengl", "vulkan"]
    }
    readonly property var renderBackendNames: {
        if (Qt.platform.os === "android")
            return [qsTr("Automatic"), qsTr("OpenGL ES"), qsTr("Vulkan")]
        if (Qt.platform.os === "windows")
            return [qsTr("Automatic"), qsTr("Direct3D 11"), qsTr("OpenGL"), qsTr("Vulkan")]
        return [qsTr("Automatic"), qsTr("OpenGL"), qsTr("Vulkan")]
    }

    function creditNameText(credit) {
        if (!credit.url)
            return credit.name
        return "<a href=\"" + credit.url + "\">" + credit.name + "</a>"
    }

    function creditLicenseText(credit) {
        if (!credit.licenseUrl)
            return credit.license
        return "<a href=\"" + credit.licenseUrl + "\">" + credit.license + "</a>"
    }

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
        anchors.margins: Theme.gapSection
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
            ScrollBar.vertical: AppScrollBar {}
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
                        font.pixelSize: Theme.fontHuge
                    }
                    AppButton {
                        visible: !page.compact
                        text: qsTr("Save")
                        primary: true
                        onClicked: App.settings.save()
                    }
                }
                SettingsCategory {
                    category: 0
                    SettingGroup {
                        title: qsTr("Appearance")
                        SettingComboRow {
                            label: qsTr("Theme")
                            setting: "colorTheme"
                            values: page.colorThemeValues
                            labels: [qsTr("Light"), qsTr("Dark"), qsTr("Black"), qsTr("System")]
                        }
                        SettingComboRow {
                            label: qsTr("Language")
                            setting: "language"
                            values: ["zh_CN", "en"]
                            labels: [qsTr("Chinese"), qsTr("English")]
                        }
                        // The icon theme stays in config.ini and is not offered here.
                        SettingSpinRow {
                            label: qsTr("Font size")
                            setting: "fontSize"
                            from: 6
                            to: 96
                        }
                        SettingSwitchRow {
                            label: qsTr("Collapse sidebar on startup")
                            setting: "collapseSidebarOnStartup"
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
                                        delegate: AbstractButton {
                                            id: swatch
                                            required property string modelData
                                            width: Theme.touch ? Theme.controlHeight : 18
                                            height: width
                                            padding: 0
                                            hoverEnabled: true
                                            focusPolicy: Qt.TabFocus
                                            background: null
                                            onClicked: App.settings.accentColor = swatch.modelData
                                            contentItem: Rectangle {
                                                anchors.centerIn: parent
                                                width: Theme.touch ? Theme.iconLargeSize : 18
                                                height: width
                                                radius: width / 2
                                                color: swatch.modelData
                                                border.width: App.settings.accentColor === swatch.modelData ? 2 : (
                                                                                                                  swatch.visualFocus
                                                                                                                  ? Theme.borderWidth
                                                                                                                    * 3 : 1)
                                                border.color: App.settings.accentColor === swatch.modelData
                                                              ? Theme.text : (swatch.pressed || swatch.visualFocus
                                                                              ? Theme.accent : Theme.border)
                                            }
                                            Accessible.name: swatch.modelData
                                        }
                                    }
                                }
                                RowLayout {
                                    spacing: Theme.gapSmall
                                    AppColorField {
                                        id: accentField
                                        Layout.fillWidth: true
                                        onEdited: color => App.settings.accentColor = color
                                    }
                                    Binding {
                                        target: accentField
                                        property: "value"
                                        value: App.settings.accentColor
                                        restoreMode: Binding.RestoreBindingOrValue
                                    }
                                }
                            }
                        }
                    }
                }
                SettingsCategory {
                    category: 1
                    SettingGroup {
                        title: qsTr("Initial view")
                        SettingComboRow {
                            label: qsTr("Initial map position")
                            setting: "initialMapPosition"
                            values: page.initialMapPositionValues
                            labels: page.initialMapPositionNames
                        }
                    }
                    SettingGroup {
                        title: qsTr("Interface")
                        SettingSwitchRow {
                            label: qsTr("Show debug panel button")
                            setting: "showMapDebugButton"
                        }
                        SettingSwitchRow {
                            label: qsTr("Show status bar")
                            setting: "showMapStatusBar"
                        }
                        SettingSwitchRow {
                            label: qsTr("Show map crosshair")
                            setting: "showMapCrosshair"
                        }
                    }
                    SettingGroup {
                        title: qsTr("Zoom and appearance")
                        SettingComboRow {
                            label: qsTr("Minimum scale")
                            setting: "minimumScaleLevel"
                            values: page.zoomScaleLevels.filter(scale => scale <= App.settings.maximumScaleLevel)
                        }
                        SettingComboRow {
                            label: qsTr("Maximum scale")
                            setting: "maximumScaleLevel"
                            values: page.zoomScaleLevels.filter(scale => scale >= App.settings.minimumScaleLevel)
                        }
                        SettingSliderRow {
                            label: qsTr("Pan inertia")
                            setting: "panInertia"
                            from: 0
                            to: 1
                        }
                        SettingColorRow {
                            label: qsTr("Grid color")
                            setting: "gridLineColor"
                        }
                        SettingColorRow {
                            label: qsTr("Selection color")
                            setting: "mapSelectionColor"
                        }
                        SettingComboRow {
                            label: qsTr("Empty tile colors")
                            setting: "tileColorScheme"
                            values: ["dark", "light"]
                            labels: [qsTr("Dark"), qsTr("Light")]
                        }
                    }
                    SettingGroup {
                        title: qsTr("Actors")
                        SettingComboRow {
                            label: qsTr("Actor style")
                            setting: "actorRenderStyle"
                            values: [0, 1]
                            labels: [qsTr("Individual"), qsTr("Consolidated")]
                        }
                        SettingSpinRow {
                            label: qsTr("Border width")
                            setting: "actorBorderWidth"
                            from: 0
                            to: 32
                        }
                        SettingColorRow {
                            label: qsTr("Border color")
                            setting: "actorBorderColor"
                        }
                    }
                    SettingGroup {
                        title: qsTr("3D view editor")
                        SettingColorRow {
                            label: qsTr("Editor color")
                            setting: "chunkEditorHighlightColor"
                        }
                        SettingSpinRow {
                            label: qsTr("Editor width")
                            setting: "chunkEditorHighlightWidth"
                            from: 0
                            to: 64
                        }
                        SettingColorRow {
                            label: qsTr("Voxel selection color")
                            setting: "voxelSelectionColor"
                        }
                    }
                }
                SettingsCategory {
                    category: 2
                    SettingGroup {
                        title: qsTr("Caches")
                        SettingSpinRow {
                            label: qsTr("Region cache size")
                            setting: "regionCacheSize"
                            from: 0
                            to: 10000000
                        }
                        SettingSpinRow {
                            label: qsTr("Empty region cache size")
                            setting: "emptyRegionCacheSize"
                            from: 0
                            to: 10000000
                        }
                        SettingSpinRow {
                            label: qsTr("Background threads")
                            setting: "threadCount"
                            from: 1
                            to: 128
                        }
                    }
                }
                SettingsCategory {
                    category: 4
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
                                    font.pixelSize: Theme.fontSmall
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.path
                                    color: Theme.muted
                                    font.pixelSize: Theme.fontSmall
                                    elide: Text.ElideMiddle
                                }
                                Text {
                                    visible: !modelData.path
                                    text: qsTr("Not configured")
                                    color: Theme.warning
                                    font.pixelSize: Theme.fontSmall
                                }
                                AppIconButton {
                                    visible: modelData.removable
                                    iconName: "close"
                                    label: qsTr("Remove world folder")
                                    buttonSize: Theme.iconButtonSmallSize
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
                        SettingSwitchRow {
                            label: qsTr("Load global data")
                            setting: "loadGlobalData"
                        }
                        SettingSpinRow {
                            label: qsTr("Maximum global data entries")
                            setting: "maxGlobalDataLoadCount"
                            from: 0
                            to: 10000000
                            controlEnabled: App.settings.loadGlobalData
                        }
                        SettingSwitchRow {
                            label: qsTr("Preload chunk coordinates")
                            setting: "preloadAllChunkCoords"
                        }
                    }
                    SettingGroup {
                        title: qsTr("LevelDB XOR")
                        SettingComboRow {
                            label: qsTr("Mode")
                            setting: "leveldbXorMode"
                            values: [0, 1, 2]
                            labels: [qsTr("Disabled"), qsTr("Enabled"), qsTr("Auto detect")]
                        }
                        SettingTextRow {
                            label: qsTr("Secret key")
                            setting: "leveldbXorKey"
                        }
                    }
                    SettingGroup {
                        title: qsTr("Startup")
                        SettingSwitchRow {
                            label: qsTr("Scan LeviLauncher paths")
                            setting: "scanLeviPath"
                        }
                    }
                }
                SettingsCategory {
                    category: 3
                    SettingGroup {
                        title: qsTr("Backend")
                        SettingComboRow {
                            label: qsTr("Graphics backend")
                            setting: "renderBackend"
                            values: page.renderBackendValues
                            labels: page.renderBackendNames
                        }
                    }
                    SettingGroup {
                        title: qsTr("Shadows and occlusion")
                        SettingSliderRow {
                            label: qsTr("Shadow strength")
                            setting: "gpuShadowStrength"
                            from: 0
                            to: 1
                        }
                        SettingComboRow {
                            label: qsTr("Shadow precision")
                            setting: "gpuShadowStep"
                            values: [4, 8, 16, 32]
                        }
                        SettingSliderRow {
                            label: qsTr("Ambient occlusion")
                            setting: "gpuAoStrength"
                            from: 0
                            to: 1
                        }
                        SettingComboRow {
                            label: qsTr("AO quality")
                            setting: "gpuAoSteps"
                            values: [2, 4, 8]
                            labels: [qsTr("Low"), qsTr("Medium"), qsTr("High")]
                        }
                    }
                    SettingGroup {
                        title: qsTr("Terrain appearance")
                        SettingSliderRow {
                            label: qsTr("Saturation")
                            setting: "gpuSaturation"
                            from: 0
                            to: 2
                        }
                        SettingSliderRow {
                            label: qsTr("Brightness")
                            setting: "gpuBrightness"
                            from: 0
                            to: 2
                        }
                        SettingSliderRow {
                            label: qsTr("Bevel strength")
                            setting: "gpuBevelStrength"
                            from: 0
                            to: 1
                        }
                        SettingSliderRow {
                            label: qsTr("Bevel width")
                            setting: "gpuBevelWidth"
                            from: 0.25
                            to: 2
                        }
                        SettingSliderRow {
                            label: qsTr("Biome blend blocks")
                            setting: "gpuBiomeBlendBlocks"
                            from: 0
                            to: 16
                        }
                    }
                    SettingGroup {
                        title: qsTr("Grass height shading")
                        SettingSwitchRow {
                            label: qsTr("Enabled")
                            setting: "gpuGrassHeightEnabled"
                        }
                        SettingSpinRow {
                            label: qsTr("Base height")
                            setting: "gpuGrassHeightBase"
                            from: -128
                            to: 512
                            controlEnabled: App.settings.gpuGrassHeightEnabled
                        }
                        SettingSpinRow {
                            label: qsTr("Range")
                            setting: "gpuGrassHeightRange"
                            from: 1
                            to: 512
                            controlEnabled: App.settings.gpuGrassHeightEnabled
                        }
                        SettingColorRow {
                            label: qsTr("Marker color")
                            setting: "gpuGrassHeightColor"
                            controlEnabled: App.settings.gpuGrassHeightEnabled
                        }
                    }
                    SettingGroup {
                        title: qsTr("Performance")
                        SettingSwitchRow {
                            label: qsTr("High-detail terrain cache")
                            setting: "gpuHighDetailMap"
                            tooltip: qsTr(
                                         "Keep full-resolution terrain and shading further out when zooming. Raises terrain tile arrays from about 96 MiB to 256 MiB and builds more tiles.")
                        }
                        SettingSpinRow {
                            label: qsTr("Tile snapshot cache (MiB)")
                            setting: "gpuTileSnapshotCacheSizeMB"
                            from: 8
                            to: 512
                        }
                    }
                }
                SettingsCategory {
                    category: 5
                    SettingGroup {
                        title: qsTr("Application information")
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gapLarge
                            Rectangle {
                                Layout.preferredWidth: Theme.ui(64)
                                Layout.preferredHeight: Theme.ui(64)
                                Layout.alignment: Qt.AlignTop
                                radius: Theme.radiusLarge
                                color: Theme.accentSoft
                                Image {
                                    anchors.centerIn: parent
                                    width: Theme.ui(48)
                                    height: Theme.ui(48)
                                    source: "qrc:/bedrockmap/icons/app.svg"
                                    sourceSize.width: width * 2
                                    sourceSize.height: height * 2
                                    smooth: true
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                spacing: Theme.gapTiny
                                Text {
                                    Layout.fillWidth: true
                                    text: Qt.application.name
                                    color: Theme.text
                                    font.pixelSize: Theme.fontHuge
                                    wrapMode: Text.Wrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: Qt.application.version + "  ·  " + qsTranslate("AboutDialog",
                                                                                         "aboutDialog.copyright")
                                    color: Theme.muted
                                    font.pixelSize: Theme.fontMedium
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.gapSmall
                            text: qsTranslate("AboutDialog", "aboutDialog.description")
                            color: Theme.text
                            font.pixelSize: Theme.fontMedium
                            lineHeight: 1.3
                            wrapMode: Text.WordWrap
                        }
                        Flow {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.gapSmall
                            Layout.preferredHeight: implicitHeight
                            spacing: Theme.gapSmall
                            AppButton {
                                primary: true
                                text: App.checkingForUpdates ? qsTr("Checking...") : qsTr("Check for updates")
                                enabled: !App.checkingForUpdates
                                onClicked: App.checkForUpdates(true)
                            }
                            AppButton {
                                text: qsTranslate("AboutDialog", "aboutDialog.github")
                                onClicked: Qt.openUrlExternally("https://github.com/bedrock-dev/BedrockMap")
                            }
                            AppButton {
                                text: qsTranslate("AboutDialog", "aboutDialog.joinQQGroup")
                                onClicked: Qt.openUrlExternally("https://qm.qq.com/q/3KJZZCkDbW")
                            }
                        }
                        AppCheckBox {
                            id: startupUpdateCheck
                            Layout.fillWidth: true
                            text: qsTr("Don't check for updates on startup")
                            onToggled: App.settings.checkUpdate = !checked
                        }
                        Binding {
                            target: startupUpdateCheck
                            property: "checked"
                            value: !App.settings.checkUpdate
                            restoreMode: Binding.RestoreBindingOrValue
                        }
                    }
                    SettingGroup {
                        title: qsTr("Developers")
                        PersonRow {
                            primary: true
                            name: "hhhxiao"
                            url: "https://github.com/hhhxiao"
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.gapSmall
                            Layout.bottomMargin: Theme.gapSmall
                            implicitHeight: Theme.borderWidth
                            color: Theme.border
                        }
                        PersonRow {
                            name: "INTZOT"
                            role: qsTr("Contributor")
                            url: "https://space.bilibili.com/382492603"
                        }
                        PersonRow {
                            name: "OEOTYAN"
                            role: qsTr("Contributor")
                            url: "https://github.com/OEOTYAN"
                        }
                    }
                    SettingGroup {
                        title: qsTr("Open-source credits")
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gapTiny
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.gapMedium
                                Text {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 0
                                    text: qsTr("Component")
                                    color: Theme.muted
                                    font.pixelSize: Theme.fontSmall
                                }
                                Text {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 0
                                    text: qsTr("License")
                                    color: Theme.muted
                                    font.pixelSize: Theme.fontSmall
                                }
                            }
                            Repeater {
                                model: page.openSourceCredits
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: Theme.gapMedium
                                    CreditText {
                                        credit: modelData
                                        showName: true
                                    }
                                    CreditText {
                                        credit: modelData
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    component SettingsCategory: ColumnLayout {
        property int category
        visible: category === page.category
        Layout.fillWidth: true
        spacing: Theme.gapMedium
    }
    component SettingGroup: AppCard {
        property string title
        default property alias content: body.data
        Layout.fillWidth: true
        // Bounded by construction: a group must never inflate the scroll content,
        // or the trailing controls end up outside the visible viewport.
        Layout.preferredWidth: 0
        implicitHeight: titleLabel.implicitHeight + body.implicitHeight + Theme.gapLarge * 2 + Theme.gapSmall
        Label {
            id: titleLabel
            x: Theme.gapLarge
            y: Theme.gapLarge
            text: title
            color: Theme.text
            font.pixelSize: Theme.fontLarge
        }
        ColumnLayout {
            id: body
            anchors.left: parent.left
            anchors.leftMargin: Theme.gapLarge
            anchors.right: parent.right
            anchors.rightMargin: Theme.gapLarge
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
            font.pixelSize: Theme.fontMedium
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        // The second child supplied by each row occupies the trailing column.
    }
    component SettingSwitchRow: SettingRow {
        id: row
        property string setting
        property alias checked: control.checked
        property alias tooltip: control.tooltip
        AppSwitch {
            id: control
            onToggled: App.settings[row.setting] = checked
        }
        Binding {
            target: control
            property: "checked"
            value: App.settings[row.setting]
            restoreMode: Binding.RestoreBindingOrValue
        }
    }
    component SettingComboRow: SettingRow {
        id: row
        property string setting
        property var values
        property var labels: values
        AppComboBox {
            id: control
            model: row.labels
            onActivated: App.settings[row.setting] = row.values[currentIndex]
        }
        Binding {
            target: control
            property: "currentIndex"
            value: row.values ? Math.max(0, row.values.indexOf(App.settings[row.setting])) : 0
            restoreMode: Binding.RestoreBindingOrValue
        }
    }
    component SettingSpinRow: SettingRow {
        id: row
        property string setting
        property int from
        property int to
        property bool controlEnabled: true
        AppSpinBox {
            id: control
            enabled: row.controlEnabled
            from: row.from
            to: row.to
            onValueModified: App.settings[row.setting] = value
        }
        Binding {
            target: control
            property: "value"
            value: App.settings[row.setting]
            restoreMode: Binding.RestoreBindingOrValue
        }
    }
    component SettingSliderRow: SettingRow {
        id: row
        property string setting
        property real from
        property real to
        AppSlider {
            id: control
            Layout.preferredWidth: 220
            from: row.from
            to: row.to
            onMoved: App.settings[row.setting] = value
        }
        Binding {
            target: control
            property: "value"
            value: App.settings[row.setting]
            restoreMode: Binding.RestoreBindingOrValue
        }
    }
    component SettingColorRow: SettingRow {
        id: row
        property string setting
        property bool controlEnabled: true
        AppColorField {
            id: control
            enabled: row.controlEnabled
            onEdited: color => App.settings[row.setting] = color
        }
        Binding {
            target: control
            property: "value"
            value: App.settings[row.setting]
            restoreMode: Binding.RestoreBindingOrValue
        }
    }
    component SettingTextRow: SettingRow {
        id: row
        property string setting
        AppTextField {
            id: control
            onEditingFinished: App.settings[row.setting] = text
        }
        Binding {
            target: control
            property: "text"
            value: App.settings[row.setting]
            restoreMode: Binding.RestoreBindingOrValue
        }
    }
    component CreditText: Text {
        property var credit
        property bool showName: false
        Layout.fillWidth: true
        Layout.preferredWidth: 0
        Layout.minimumWidth: 0
        Layout.alignment: Qt.AlignTop
        text: showName ? page.creditNameText(credit) : page.creditLicenseText(credit)
        textFormat: Text.StyledText
        color: Theme.text
        linkColor: Theme.accent
        font.pixelSize: Theme.fontMedium
        wrapMode: Text.Wrap
        onLinkActivated: link => Qt.openUrlExternally(link)
    }
    component PersonRow: AbstractButton {
        id: person
        property string name
        property string role
        property string url
        property bool primary: false
        Layout.fillWidth: true
        implicitHeight: personRow.implicitHeight + Theme.gapSmall * 2
        padding: 0
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        onClicked: Qt.openUrlExternally(person.url)

        Keys.onReturnPressed: animateClick()
        Keys.onEnterPressed: animateClick()

        background: Rectangle {
            radius: Theme.radiusMedium
            color: person.hovered || person.pressed ? Theme.surfaceAlt : "transparent"
            border.width: person.visualFocus ? Theme.borderWidth * 3 : 0
            border.color: Theme.accent
        }

        contentItem: RowLayout {
            id: personRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.gapSmall
            anchors.rightMargin: Theme.gapSmall
            spacing: Theme.gapMedium
            Rectangle {
                Layout.preferredWidth: person.primary ? Theme.ui(44) : Theme.ui(32)
                Layout.preferredHeight: Layout.preferredWidth
                radius: width / 2
                color: person.primary ? Theme.accentSoft : Theme.surfaceAlt
                border.width: Theme.borderWidth
                border.color: person.primary ? Theme.accent : Theme.border
                Text {
                    anchors.centerIn: parent
                    text: person.name.charAt(0).toUpperCase()
                    color: person.primary ? Theme.accent : Theme.muted
                    font.pixelSize: person.primary ? Theme.fontExtraLarge : Theme.fontMedium
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    text: person.name
                    color: Theme.text
                    font.pixelSize: person.primary ? Theme.fontLarge : Theme.fontMedium
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: person.role.length > 0
                    text: person.role
                    color: Theme.muted
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                }
            }
            Text {
                text: "›"
                color: person.hovered || person.visualFocus ? Theme.accent : Theme.muted
                font.pixelSize: Theme.fontHuge
            }
        }

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        Accessible.role: Accessible.Link
        Accessible.name: person.role.length > 0 ? person.name + ", " + person.role : person.name
    }
}

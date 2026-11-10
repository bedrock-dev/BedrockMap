pragma Singleton

import QtQuick
import BedrockMap 1.0

QtObject {
    id: root

    // Window-level responsive state. Main.qml feeds windowWidth; everything
    // else consumes the derived layout policy below.
    property real windowWidth: 0
    readonly property int compactBreakpoint: 720
    readonly property bool compact: windowWidth > 0 && windowWidth < compactBreakpoint
    readonly property bool touch: Qt.platform.os === "android" || Qt.platform.os === "ios"

    // Scale mobile geometry independently from the user's font size. All values
    // are logical pixels; Qt handles the device pixel ratio separately.
    readonly property real mobileUiScale: 1.0
    readonly property real uiScale: touch ? mobileUiScale : 1.0
    readonly property int minimumTouchTarget: 32
    readonly property int touchTarget: Math.max(minimumTouchTarget, ui(32))
    readonly property int iconSmallSize: ui(16)
    readonly property int iconSize: ui(18)
    readonly property int iconLargeSize: ui(24)
    // Spacing scale on a 4px grid. Narrow windows and touch devices share the
    // dense values; touch only enlarges hit targets, not spacing.
    readonly property bool dense: compact || touch
    readonly property int gapTiny: ui(2)
    readonly property int gapSmall: ui(4)
    readonly property int gapMedium: ui(8)
    readonly property int gapLarge: ui(dense ? 8 : 12)
    readonly property int gapSection: ui(dense ? 16 : 24)
    readonly property int checkBoxSize: touch ? ui(20) : 18
    readonly property int switchWidth: ui(40)
    readonly property int switchHeight: touch ? ui(24) : 22
    readonly property int switchInset: ui(3)
    readonly property int switchThumbSize: switchHeight - switchInset * 2
    readonly property int sliderHandleSize: touch ? ui(18) : 14
    readonly property int sliderTrackHeight: ui(4)
    readonly property int spinBoxArrowWidth: touch ? ui(28) : 20
    readonly property int controlVisualHeight: touch ? Math.max(ui(32), fontSubtitle + ui(12)) : 28
    readonly property int controlHeight: touch ? Math.max(touchTarget, controlVisualHeight) : 28
    readonly property int buttonHeight: touch ? controlHeight : 32
    readonly property int compactButtonHeight: touch ? controlHeight : 28
    readonly property int segmentedHeight: touch ? Math.max(controlHeight, controlVisualHeight + gapTiny * 2) :
                                                   controlHeight + gapTiny * 2
    readonly property int menuItemHeight: touch ? controlHeight : 30
    readonly property int treeRowHeight: touch ? controlHeight : 28
    readonly property int settingRowHeight: touch ? Math.max(ui(44), controlHeight + ui(4)) : (compact ? 36 : 38)
    readonly property int sessionRowHeight: touch ? Math.max(ui(56), controlHeight + ui(8), fontSubtitle + fontCaption
                                                             + gapTiny + gapLarge * 2) : (compact ? 52 : 64)
    readonly property int iconButtonSize: ui(34)
    readonly property int iconButtonSmallSize: ui(28)
    readonly property int dirtyIndicatorSize: touch ? ui(10) : 9

    function ui(value) {
        return Math.round(value * uiScale)
    }

    // Single source of truth for the differences between the compact and
    // desktop GUI shells.
    readonly property bool showSidebar: !compact
    readonly property bool showBottomNavigation: compact
    readonly property bool showTopbar: compact
    readonly property bool showWorldCards: !compact
    readonly property real sidebarWidth: ui(232)
    readonly property real sidebarCollapsedWidth: ui(52)
    readonly property real sidebarCollapsedItemSize: sidebarCollapsedWidth - gapMedium * 2
    readonly property real bottomNavigationHeight: touch ? Math.max(ui(56), iconSize + fontCaption + ui(22)) : (compact
                                                                                                                ? 64 : 72)
    readonly property real topbarHeight: touch ? Math.max(ui(52), controlHeight + ui(8)) : (compact ? 52 : 72)

    readonly property real fontScale: App.settings.fontSize / 12.0
    readonly property bool systemDark: Application.styleHints.colorScheme !== Qt.Light
    readonly property bool black: App.settings.colorTheme === "black"
    readonly property bool dark: black || App.settings.colorTheme === "dark" || (App.settings.colorTheme
                                                                                 === "system" && systemDark)

    readonly property var palette: black ? blackPalette : dark ? darkPalette : lightPalette
    readonly property var blackPalette: themeWith({
                                                      background: "#000000",
                                                      surface: "#080808",
                                                      surfaceAlt: "#111111",
                                                      border: "#272727",
                                                      text: "#f3f3f3",
                                                      muted: "#999999",
                                                      accent: "#72d572",
                                                      accentSoft: "#17341d",
                                                      onAccent: "#071108",
                                                      danger: "#ed7785",
                                                      dangerSoft: "#3b171c",
                                                      warning: "#e6b455",
                                                      warningSoft: "#3b2f16",
                                                      map: "#030504",
                                                      overlay: "#e6000000",
                                                      overlayTranslucent: "#b8000000",
                                                      overlayBorder: "#66888888",
                                                      overlayText: "#f0f0f0",
                                                      overlayMuted: "#d0d0d0"
                                                  })
    readonly property var darkPalette: themeWith({
                                                     background: "#111419",
                                                     surface: "#191e26",
                                                     surfaceAlt: "#202631",
                                                     border: "#303846",
                                                     text: "#f1f4f8",
                                                     muted: "#929baa",
                                                     accent: "#72d572",
                                                     accentSoft: "#24452d",
                                                     onAccent: "#102116",
                                                     danger: "#ed7785",
                                                     dangerSoft: "#542d35",
                                                     warning: "#e6b455",
                                                     warningSoft: "#4a3d1d",
                                                     map: "#18241f",
                                                     overlay: "#d9212121",
                                                     overlayTranslucent: "#b8212121",
                                                     overlayBorder: "#66888888",
                                                     overlayText: "#f0f0f0",
                                                     overlayMuted: "#d0d0d0"
                                                 })
    readonly property var lightPalette: themeWith({
                                                      background: "#f4f6f8",
                                                      surface: "#ffffff",
                                                      surfaceAlt: "#edf1f4",
                                                      border: "#d9dfe6",
                                                      text: "#1a2028",
                                                      muted: "#68717e",
                                                      accent: "#318b4b",
                                                      accentSoft: "#dcefe0",
                                                      onAccent: "#ffffff",
                                                      danger: "#b3261e",
                                                      dangerSoft: "#f7dcda",
                                                      warning: "#9a6b00",
                                                      warningSoft: "#fdf1d6",
                                                      map: "#e1eee5",
                                                      overlay: "#e8ffffff",
                                                      overlayTranslucent: "#c8ffffff",
                                                      overlayBorder: "#33000000",
                                                      overlayText: "#1a2028",
                                                      overlayMuted: "#68717e"
                                                  })

    readonly property color background: palette.background
    readonly property color surface: palette.surface
    readonly property color surfaceAlt: palette.surfaceAlt
    readonly property color border: palette.border
    readonly property color text: palette.text
    readonly property color muted: palette.muted
    readonly property color accent: palette.accent
    readonly property color accentSoft: palette.accentSoft
    readonly property color onAccent: palette.onAccent
    readonly property color danger: palette.danger
    readonly property color dangerSoft: palette.dangerSoft
    readonly property color warning: palette.warning
    readonly property color warningSoft: palette.warningSoft
    readonly property color map: palette.map
    readonly property color mapCheckerBase: App.runtimeSettings.tileColorScheme === "light" ? "#ffffff" : "#141414"
    readonly property color mapCheckerAlternate: App.runtimeSettings.tileColorScheme === "light" ? "#eeeeee" : "#282828"
    readonly property color overlay: palette.overlay
    readonly property color overlayTranslucent: palette.overlayTranslucent
    readonly property color overlayBorder: palette.overlayBorder
    readonly property color overlayText: palette.overlayText
    readonly property color overlayMuted: palette.overlayMuted

    readonly property real metricScale: palette.metricScale
    readonly property real borderWidth: palette.borderWidth
    readonly property int radiusSmall: palette.radiusSmall
    readonly property int radiusControl: palette.radiusControl
    readonly property int radiusCard: palette.radiusCard
    readonly property int radiusPill: palette.radiusPill
    readonly property int fontCaption: palette.fontCaption
    readonly property int fontBody: palette.fontBody
    readonly property int fontSubtitle: palette.fontSubtitle
    readonly property int fontLabel: palette.fontLabel
    readonly property int fontTitle: palette.fontTitle
    readonly property int fontHeading: palette.fontHeading
    readonly property int fontDisplay: palette.fontDisplay

    function themeWith(palette) {
        const scale = fontScale
        const text = base => Math.max(8, Math.round(base * scale))
        return Object.assign({}, palette, accentTones(palette), {
                                 metricScale: Math.max(1.0, scale) * uiScale,
                                 borderWidth: 0.5,
                                 radiusSmall: ui(5),
                                 radiusControl: ui(8),
                                 radiusCard: ui(10),
                                 radiusPill: ui(17),
                                 fontCaption: text(10),
                                 fontBody: text(11),
                                 fontSubtitle: text(12),
                                 fontLabel: text(14),
                                 fontTitle: text(17),
                                 fontHeading: text(22),
                                 fontDisplay: text(25)
                             })
    }

    function accentTones(palette) {
        const accent = App.settings.accentColor
        if (!accent)
            return {}
        const accentLuminance = relativeLuminance(accent)
        const darkContrast = (accentLuminance + 0.05) / (relativeLuminance("#102116") + 0.05)
        const lightContrast = 1.05 / (accentLuminance + 0.05)
        return {
            accent: accent,
            accentSoft: Qt.tint(accent, "#C4" + palette.background.substring(1)),
            onAccent: darkContrast >= lightContrast ? "#102116" : "#ffffff"
        }
    }

    function relativeLuminance(value) {
        const color = Qt.darker(value, 1.0)
        const linear = c => c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4)
        return 0.2126 * linear(color.r) + 0.7152 * linear(color.g) + 0.0722 * linear(color.b)
    }
}

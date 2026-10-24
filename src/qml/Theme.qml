pragma Singleton

import QtQuick
import BedrockMap 1.0

QtObject {
    id: root

    // Window-level responsive state. Main.qml feeds windowWidth; everything
    // else consumes the derived layout policy below.
    property real windowWidth: 0
    readonly property int compactBreakpoint: 720
    readonly property int nbtCompactBreakpoint: 760
    readonly property bool compact: windowWidth > 0 && windowWidth < compactBreakpoint
    readonly property bool nbtCompact: windowWidth > 0 && windowWidth < nbtCompactBreakpoint
    readonly property bool touch: Qt.platform.os === "android" || Qt.platform.os === "ios"
    readonly property int touchTarget: 40

    // Single source of truth for the differences between the compact and
    // desktop GUI shells.
    readonly property bool showSidebar: !compact
    readonly property bool showBottomNavigation: compact
    readonly property bool showTopbar: compact
    readonly property bool showWorldCards: !compact
    readonly property real sidebarWidth: 232
    readonly property real sidebarCollapsedWidth: 56
    readonly property real bottomNavigationHeight: compact ? 64 : 72
    readonly property real topbarHeight: compact ? 52 : 72

    readonly property real fontScale: Settings.fontSize / 12.0
    readonly property bool systemDark: Application.styleHints.colorScheme !== Qt.Light
    readonly property bool dark: Settings.colorTheme === "dark"
        || (Settings.colorTheme !== "light" && systemDark)
    readonly property var palette: dark ? darkPalette : lightPalette
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
    readonly property color onAccent: dark ? "#102116" : "#ffffff"
    readonly property color danger: palette.danger
    readonly property color dangerSoft: palette.dangerSoft
    readonly property color warning: palette.warning
    readonly property color warningSoft: palette.warningSoft
    readonly property color map: palette.map
    readonly property color mapCheckerBase: Settings.tileColorScheme === "light" ? "#ffffff" : "#141414"
    readonly property color mapCheckerAlternate: Settings.tileColorScheme === "light" ? "#eeeeee" : "#282828"
    readonly property color overlay: palette.overlay
    readonly property color overlayBorder: palette.overlayBorder
    readonly property color overlayText: palette.overlayText
    readonly property color overlayMuted: palette.overlayMuted

    readonly property real metricScale: palette.metricScale
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
    readonly property real gapSmall: palette.gapSmall
    readonly property real gapMedium: palette.gapMedium
    readonly property real gapLarge: palette.gapLarge
    readonly property real gapSection: palette.gapSection
    readonly property real pageGutter: palette.pageGutter
    readonly property real cardPadding: palette.cardPadding
    readonly property real cardGap: palette.cardGap
    readonly property real settingRowHeight: palette.settingRowHeight
    readonly property int controlHeight: palette.controlHeight
    readonly property int buttonHeight: palette.buttonHeight

    function themeWith(palette) {
        const scale = fontScale
        const compactSpacing = compact || touch
        const text = base => Math.max(8, Math.round(base * scale))
        return Object.assign({}, palette, accentTones(palette), {
            metricScale: Math.max(1.0, scale),
            radiusSmall: 5,
            radiusControl: 8,
            radiusCard: 10,
            radiusPill: 17,
            fontCaption: text(10),
            fontBody: text(11),
            fontSubtitle: text(12),
            fontLabel: text(14),
            fontTitle: text(17),
            fontHeading: text(22),
            fontDisplay: text(25),
            gapSmall: compactSpacing ? 4 : 6,
            gapMedium: compactSpacing ? 5 : 10,
            gapLarge: compactSpacing ? 8 : 14,
            gapSection: compactSpacing ? 12 : 20,
            pageGutter: compactSpacing ? 10 : 22,
            cardPadding: compactSpacing ? 7 : 12,
            cardGap: compactSpacing ? 6 : 14,
            settingRowHeight: touch ? 48 : (compact ? 36 : 38),
            controlHeight: touch ? touchTarget : 28,
            buttonHeight: touch ? touchTarget : 30
        })
    }

    function accentTones(palette) {
        const accent = Settings.accentColor
        if (!accent)
            return {}
        return {
            accent: accent,
            accentSoft: Qt.tint(accent, "#C4" + palette.background.substring(1))
        }
    }
}

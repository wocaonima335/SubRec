pragma Singleton
import QtQuick

// Module U1 (ZCode). Design tokens, UI_REDESIGN.md §2.1–§2.6.
// FROZEN: token NAMES (every property below). Values may be tuned by U1 only.
// Nobody else hard-codes a colour, font size, radius or duration.
QtObject {
    readonly property bool dark: ThemeController.dark

    // §2.1.1 base
    readonly property color bgApp: dark ? "#15171C" : "#F3F4F6"
    readonly property color bgSurface: dark ? "#1E2128" : "#FFFFFF"
    readonly property color bgSurface2: dark ? "#262A33" : "#F7F8FA"
    readonly property color bgHover: dark ? "#2E333D" : "#EEF0F3"
    readonly property color bgPressed: dark ? "#363C48" : "#E3E6EB"
    readonly property color bgSelected: dark ? "#1E2A44" : "#E8EFFD"
    readonly property color bgPreview: dark ? "#0B0C0F" : "#111318"
    readonly property color bgScrim: dark ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(0, 0, 0, 0.30)
    readonly property color borderDefault: dark ? "#343A46" : "#DADDE3"
    readonly property color borderStrong: dark ? "#4A5160" : "#B9BEC8"
    readonly property color textPrimary: dark ? "#E8EAEE" : "#1A1D23"
    readonly property color textSecondary: dark ? "#A3A9B6" : "#545B68"
    readonly property color textTertiary: dark ? "#6E7583" : "#858C99"
    readonly property color textDisabled: dark ? "#545A66" : "#B3B8C2"
    readonly property color textOnAccent: "#FFFFFF"

    // §2.1.2 brand and semantic
    readonly property color accentDefault: dark ? "#3B82F6" : "#2563EB"
    readonly property color accentHover: dark ? "#5B97F8" : "#1D4ED8"
    readonly property color accentPressed: dark ? "#2F6FDB" : "#1E40AF"
    readonly property color accentText: dark ? "#7FB0FF" : "#2563EB"
    readonly property color recordDefault: dark ? "#EF4444" : "#DC2626"
    readonly property color recordHover: dark ? "#F25C5C" : "#E23B3B"
    readonly property color recordPressed: dark ? "#D73737" : "#B91C1C"
    readonly property color successFg: dark ? "#22C55E" : "#16A34A"
    readonly property color successBg: dark ? "#12301F" : "#E8F6EE"
    readonly property color warningFg: dark ? "#F59E0B" : "#D97706"
    readonly property color warningBg: dark ? "#3A2A0C" : "#FDF3E3"
    readonly property color dangerFg: dark ? "#EF4444" : "#DC2626"
    readonly property color dangerBg: dark ? "#3B1618" : "#FDECEC"
    readonly property color dangerBorder: dark ? "#7F2A2E" : "#F5B5B5"
    readonly property color infoFg: dark ? "#38BDF8" : "#0284C7"
    readonly property color infoBg: dark ? "#0F2A3A" : "#E6F4FB"
    readonly property color focusRing: dark ? Qt.rgba(0.231, 0.510, 0.965, 0.60) : Qt.rgba(0.145, 0.388, 0.922, 0.50)

    // §2.1.3 window buttons, §2.5.10 tooltip
    readonly property color windowButtonHover: dark ? "#2E333D" : "#E3E6EB"
    readonly property color closeButtonHover: "#C42B1C"
    readonly property color tooltipBg: dark ? "#3A404C" : "#1A1D23"
    readonly property color previewChipBg: Qt.rgba(0, 0, 0, 0.60)

    // §2.2 typography
    readonly property string fontFamily: "Microsoft YaHei UI"
    readonly property string monoFamily: "Cascadia Mono" // U1: fall back to Consolas when missing
    readonly property font timerFont: Qt.font({ family: monoFamily, pixelSize: 32, weight: Font.DemiBold })
    readonly property font h1Font: Qt.font({ family: fontFamily, pixelSize: 20, weight: Font.DemiBold })
    readonly property font h2Font: Qt.font({ family: fontFamily, pixelSize: 16, weight: Font.DemiBold })
    readonly property font bodyFont: Qt.font({ family: fontFamily, pixelSize: 14 })
    readonly property font bodyStrongFont: Qt.font({ family: fontFamily, pixelSize: 14, weight: Font.DemiBold })
    readonly property font captionFont: Qt.font({ family: fontFamily, pixelSize: 12 })
    readonly property font badgeFont: Qt.font({ family: fontFamily, pixelSize: 11, weight: Font.DemiBold })
    readonly property font monoSmallFont: Qt.font({ family: monoFamily, pixelSize: 12 })

    // §2.3 spacing and sizes
    readonly property int space4: 4
    readonly property int space8: 8
    readonly property int space12: 12
    readonly property int space16: 16
    readonly property int space24: 24
    readonly property int space32: 32
    readonly property int controlHeight: 36
    readonly property int compactControlHeight: 32 // compact ComboBox (§2.5.3)
    readonly property int smallControlHeight: 28
    readonly property int listRowHeight: 56
    readonly property int navItemHeight: 40
    readonly property int formRowHeight: 64
    readonly property int formWideRowHeight: 98

    // §2.4 radius
    readonly property int radiusXs: 4
    readonly property int radiusSm: 6
    readonly property int radiusMd: 8
    readonly property int radiusLg: 12

    // §2.6 motion (ms)
    readonly property int durationHover: 100
    readonly property int durationToggle: 150
    readonly property int durationPage: 150
    readonly property int durationDialog: 150
    readonly property int blinkPeriod: 1000
    readonly property bool reduceMotion: ThemeController.reduceMotion

    // §2.7 / §3.1 layout
    readonly property int titleBarHeight: 40
    readonly property int navWidth: 200
    readonly property int navCollapsedWidth: 64
    readonly property int narrowBreakpoint: 1100
    readonly property int pagePadding: 24
    readonly property int sourcePanelWidth: 280
    readonly property int minWindowWidth: 960
    readonly property int minWindowHeight: 640
    readonly property int defaultWindowWidth: 1280
    readonly property int defaultWindowHeight: 800
}

import QtQuick

// Module U2 (ZCode). §2.5.8 / §3.2.4 control-bar subtitle badge ("CC" icon instead of dot).
// FROZEN API:
//   uiState: SubtitleView.uiState ("off" | "needModel" | "downloading" | "unavailable" |
//            "ready" | "live" | "lagging" | "finishing" | "done" | "failed")
//   mode: "live" | "after" (U6 text differs), progressPercent (U3/U8), lagSeconds (U7)
//   degradeLevel / degradeReason (design-live-v2 §6.2–6.3): live + L3 shows 「录完后生成」;
//   lagging reached from L3 (restoreLive, never automatic per §6.3) shows 「恢复中」 until
//   the level leaves L2; degradeReason is the tooltip while degraded.
//   signal clicked(): owner decides (→ P3-4 or openLastSrtFolder) per §3.2.4 table
Rectangle {
    id: root
    property string uiState: "off"
    property string mode: "live"
    property int progressPercent: 0
    property real lagSeconds: 0
    property int degradeLevel: 0
    property string degradeReason: ""
    signal clicked()

    // §6.3: L3 never rises on its own, so live+L3 → lagging is the user's restoreLive();
    // keep showing 恢复中 while the catch-up runs, clear it when the level leaves L2.
    property bool __recovering: false
    property bool __wasL3: false
    onDegradeLevelChanged: {
        if (root.degradeLevel >= 3 || root.degradeLevel < 2)
            root.__recovering = false
        else if (root.__wasL3)
            root.__recovering = true
        root.__wasL3 = root.degradeLevel >= 3
    }
    onUiStateChanged: {
        if (root.uiState !== "live" && root.uiState !== "lagging")
            root.__recovering = false
    }

    readonly property color __bg: uiState === "off" ? Theme.bgSurface2
                                : kindOf(uiState) === "success" ? Theme.successBg
                                : kindOf(uiState) === "warning" ? Theme.warningBg
                                : kindOf(uiState) === "danger" ? Theme.dangerBg
                                : Theme.infoBg
    readonly property color __fg: uiState === "off" ? Theme.textTertiary
                                : kindOf(uiState) === "success" ? Theme.successFg
                                : kindOf(uiState) === "warning" ? Theme.warningFg
                                : kindOf(uiState) === "danger" ? Theme.dangerFg
                                : Theme.infoFg
    readonly property string __label: {
        switch (uiState) {
        case "needModel": return qsTr("字幕：需下载模型")
        case "downloading": return qsTr("字幕：下载中 %1%").arg(progressPercent)
        case "unavailable": return qsTr("字幕：不可用")
        case "ready": return qsTr("字幕：已就绪")
        case "live": {
            // §6.3: L3 keeps state "live"; the level says the SRT comes after recording.
            if (root.degradeLevel >= 3)
                return qsTr("字幕：录完后生成")
            return root.mode === "after" ? qsTr("字幕：录后生成") : qsTr("字幕：识别中")
        }
        case "lagging":
            return root.__recovering ? qsTr("字幕：恢复中")
                                     : qsTr("字幕：落后 %1 秒").arg(Math.round(lagSeconds))
        case "finishing": return qsTr("字幕：生成中 %1%").arg(progressPercent)
        case "done": return qsTr("字幕：已生成")
        case "failed": return qsTr("字幕：失败")
        default: return qsTr("字幕：关")
        }
    }

    function kindOf(state: string): string {
        switch (state) {
        case "ready":
        case "done":
            return "success"
        case "needModel":
        case "lagging":
            return "warning"
        case "unavailable":
        case "failed":
            return "danger"
        default:
            return "info"
        }
    }

    implicitHeight: 24
    implicitWidth: 20 + label.implicitWidth + 10
    radius: 12
    color: __bg

    AppIcon {
        id: ccIcon
        x: 10
        anchors.verticalCenter: parent.verticalCenter
        name: "captions"
        size: 12
        color: root.__fg
    }

    Text {
        id: label
        anchors.left: ccIcon.right
        anchors.leftMargin: Theme.space8 - 2
        anchors.verticalCenter: parent.verticalCenter
        text: root.__label
        font: Theme.badgeFont
        color: root.__fg
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    // §6.2: the L2+ reason (内存不足 / 优先保证录制流畅 / …) on hover.
    HoverHandler {
        id: degradeHover
    }
    AppToolTip {
        visible: degradeHover.hovered && root.degradeLevel >= 2 && root.degradeReason.length > 0
        text: root.degradeReason
    }
}

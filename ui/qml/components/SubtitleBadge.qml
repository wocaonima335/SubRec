import QtQuick

// Module U2 (ZCode). §2.5.8 / §3.2.4 control-bar subtitle badge ("CC" icon instead of dot).
// FROZEN API:
//   uiState: SubtitleView.uiState ("off" | "needModel" | "downloading" | "unavailable" |
//            "ready" | "live" | "lagging" | "finishing" | "done" | "failed")
//   mode: "live" | "after" (U6 text differs), progressPercent (U3/U8), lagSeconds (U7)
//   signal clicked(): owner decides (→ P3-4 or openLastSrtFolder) per §3.2.4 table
Rectangle {
    id: root
    property string uiState: "off"
    property string mode: "live"
    property int progressPercent: 0
    property real lagSeconds: 0
    signal clicked()

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
        case "live": return mode === "after" ? qsTr("字幕：录后生成") : qsTr("字幕：识别中")
        case "lagging": return qsTr("字幕：落后 %1 秒").arg(Math.round(lagSeconds))
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
}

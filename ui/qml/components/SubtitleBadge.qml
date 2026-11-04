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

    implicitHeight: 24
    implicitWidth: 96
    radius: 12
    color: Theme.bgSurface2

    // TODO(U2): texts and colours per §3.2.4 column "字幕标签".
}

import QtQuick

// Module U2 (ZCode). §2.5.8 status pill, 24 high, radius 12, 8 px dot, `type/badge` text.
// FROZEN API:
//   uiState: RecorderView.uiState values ("noDevice" | "idle" | "preparing" | "running" |
//            "paused" | "stopping" | "failed") -> colours, dot/spinner, default text
//   text: override the default text (e.g. title-bar mini indicator "录制中 00:12:34")
//   clickable + signal clicked() (title-bar mini indicator jumps to P1)
Rectangle {
    id: root
    property string uiState: "idle"
    property string text: ""
    property bool clickable: false
    signal clicked()

    implicitHeight: 24
    implicitWidth: 72
    radius: 12
    color: Theme.bgSurface2

    // TODO(U2): per-state colours/texts from the §2.5.8 table, blinking dot in "running"
    // (Theme.blinkPeriod, steady when Theme.reduceMotion), spinner in preparing/stopping.
}

import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.7 RecordMain, 64×64 circle.
// FROZEN API:
//   uiState: RecorderView.uiState. idle/failed -> white dot (start); running/paused -> white
//            rounded square (stop); preparing/stopping -> spinner, not clickable; noDevice ->
//            disabled 40 %.
//   toolTipText: owner supplies, e.g. "开始录制（F9 / Ctrl+Alt+R）"
//   signal clicked(): owner calls ShellView.requestStart() or requestStop() by state.
AbstractButton {
    id: root
    property string uiState: "idle"
    property string toolTipText: ""

    implicitWidth: 64
    implicitHeight: 64
    hoverEnabled: true
    enabled: uiState !== "noDevice" && uiState !== "preparing" && uiState !== "stopping"

    // TODO(U2): glyphs per state, hover/pressed colours, focus ring, tooltip.
    background: Rectangle {
        radius: width / 2
        color: Theme.recordDefault
        opacity: root.enabled ? 1.0 : 0.4
    }
}

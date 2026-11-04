import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.7 RecordPause, 44×44 circle; visible only in running/paused
// (150 ms fade, keeps its layout slot so the main button never moves).
// FROZEN API: uiState (RecorderView.uiState), toolTipText; signal clicked() -> owner calls
// ShellView.requestPause() / requestResume().
AbstractButton {
    id: root
    property string uiState: "idle"
    property string toolTipText: ""
    readonly property bool paused: uiState === "paused"

    implicitWidth: 44
    implicitHeight: 44
    hoverEnabled: true
    opacity: uiState === "running" || uiState === "paused" ? 1 : 0
    enabled: opacity > 0

    // TODO(U2): pause bars / play triangle, paused colours (warning/*), fade animation.
    background: Rectangle {
        radius: width / 2
        color: root.paused ? Theme.warningBg : Theme.bgSurface2
        border.width: 1
        border.color: root.paused ? Theme.warningFg : Theme.borderDefault
    }
}

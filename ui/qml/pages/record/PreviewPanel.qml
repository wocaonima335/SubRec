import QtQuick

// Module U5 (ZCode). P1 area B: preview container (radius 8, bg/preview, border by state:
// running 2 px record/default, paused 2 px warning/fg, failed danger/border), source chip
// ("屏幕 · 主显示器"), overlays: S0 no-device EmptyState, S2 spinner, S4 "已暂停" pill,
// S5 "正在保存…" pill, S6 error EmptyState (retry -> ShellView.requestStart(), refresh ->
// RecorderView.refreshCaptureDevices(), copy -> DesktopActions.copyText + Toast T7,
// extra line from RecorderView.recordingFailed partial path).
// FROZEN API: none.
Rectangle {
    id: root

    radius: Theme.radiusMd
    color: Theme.bgPreview
    border.width: 1
    border.color: Theme.borderDefault

    PreviewSurface {
        anchors.fill: parent
        anchors.margins: 1
    }

    // TODO(U5): chip, overlays S0/S2/S4/S5/S6, state borders.
}

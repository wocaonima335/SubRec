import QtQuick

// Module U4 (ZCode). D1 exit confirmation, variants D1a..D1d (UI_REDESIGN §3.6 D1).
// FROZEN API: openFor(variant 0..3, timeText); signals confirmed(), cancelled().
// D1d ("正在保存录像", single "知道了") closes itself when RecorderView.uiState leaves
// "stopping" and then emits cancelled() (review S5).
AppDialog {
    id: root
    property int variant: 0
    property string timeText: ""
    signal confirmed()
    signal cancelled()

    title: variant === 2 ? "字幕还在生成" : variant === 3 ? "正在保存录像" : "正在录制，确定退出吗？"
    primaryKind: variant === 3 ? "primary" : "danger"
    primaryText: variant === 3 ? "知道了" : variant === 2 ? "仍然退出" : "停止并退出"
    secondaryText: variant === 3 ? "" : "取消"

    function openFor(v: int, time: string) {
        root.variant = v
        root.timeText = time
        open()
    }

    // TODO(U4): body texts per variant; D1d auto-close.
    onAccepted: { close(); if (variant === 3) cancelled(); else confirmed() }
    onRejected: { close(); cancelled() }
}

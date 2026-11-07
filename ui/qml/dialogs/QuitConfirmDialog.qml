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

    title: variant === 2 ? subtitleForVariant()
         : variant === 3 ? qsTr("正在保存录像")
         : qsTr("正在录制，确定退出吗？")
    primaryKind: variant === 3 ? "primary" : "danger"
    primaryText: variant === 3 ? qsTr("知道了") : variant === 2 ? qsTr("仍然退出") : qsTr("停止并退出")
    secondaryText: variant === 3 ? "" : qsTr("取消")

    function subtitleForVariant() {
        return root.timeText.length > 0 ? qsTr("字幕还在生成（%1%）").arg(root.timeText)
                                        : qsTr("字幕还在生成")
    }

    function openFor(v: int, time: string) {
        root.variant = v
        root.timeText = time
        open()
    }

    onAccepted: {
        close()
        if (variant === 3)
            cancelled() // "知道了" just acknowledges; the app stays alive
        else
            confirmed()
    }
    onRejected: {
        close()
        cancelled()
    }

    // D1d: saving finished while the dialog is up -> close quietly, no exit (S5).
    Connections {
        target: RecorderView
        function onUiStateChanged() {
            if (root.visible && root.variant === 3 && RecorderView.uiState !== "stopping")
                root.reject()
        }
    }

    Column {
        spacing: Theme.space4

        Text {
            width: 392
            text: root.variant === 2
                ? qsTr("现在退出只会保存已完成的部分，下次启动不会继续生成。")
                : root.variant === 3
                ? qsTr("请等保存完成后再退出。")
                : qsTr("退出前会先停止录制，并保存已录的 %1。").arg(root.timeText)
            font: Theme.bodyFont
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
        }

        // D1b (subtitle on): second paragraph about the 3 s subtitle grace period.
        Text {
            visible: root.variant === 1
            width: 392
            text: qsTr("字幕最多再等 3 秒，来不及生成的部分不会保存。")
            font: Theme.bodyFont
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
        }
    }
}

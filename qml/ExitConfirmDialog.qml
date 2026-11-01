import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// D1 退出确认 (UI_REDESIGN.md D1, revised by MINIMIZE_DESIGN.md §6.6).
// variant: 0 = D1a, 1 = D1b, 2 = D1c, 3 = D1d. Batch 1 has no subtitles, so in
// practice only D1a / D1d occur; D1b / D1c texts are present for completeness.
// Emits confirmed() (stop and exit) or cancelled() (cancel / 知道了).
Dialog {
    id: dialog
    property int variant: 0
    property string timeText: ""
    property int subtitleProgress: 0
    signal confirmed()
    signal cancelled()

    modal: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: 440
    padding: 24

    function openFor(v, time) { variant = v; timeText = time; open() }

    readonly property string titleText: variant === 2 ? "字幕还在生成（" + subtitleProgress + "%）"
                                      : variant === 3 ? "正在保存录像" : "正在录制，确定退出吗？"

    background: Rectangle { color: "#2B2E38"; radius: 8; border.color: "#40454F" }
    header: Text {
        text: dialog.titleText; color: "#FFFFFF"; font.pixelSize: 18; font.bold: true
        leftPadding: 24; topPadding: 24
    }

    contentItem: ColumnLayout {
        spacing: 12
        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#B0B4BC"; font.pixelSize: 14
            text: dialog.variant === 2 ? "现在退出只会保存已完成的部分，下次启动不会继续生成。"
                : dialog.variant === 3 ? "请等保存完成后再退出。"
                : "退出前会先停止录制，并保存已录的 " + dialog.timeText + "。"
        }
        Text {
            visible: dialog.variant === 1
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#B0B4BC"; font.pixelSize: 14
            text: "字幕最多再等 3 秒，来不及生成的部分不会保存。"
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            Button {
                visible: dialog.variant !== 3
                text: "取消"
                onClicked: { dialog.close(); dialog.cancelled() }
            }
            Button {
                text: dialog.variant === 3 ? "知道了" : dialog.variant === 2 ? "仍然退出" : "停止并退出"
                highlighted: true
                onClicked: {
                    dialog.close()
                    if (dialog.variant === 3) dialog.cancelled(); else dialog.confirmed()
                }
            }
        }
    }
    onRejected: cancelled()
}

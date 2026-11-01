import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// D7 "关闭字录窗口" (MINIMIZE_DESIGN.md §6.6). D7-R when capturing.
// Emits chosen(toTray, remember) or cancelled(); the owner calls
// shell.resolveCloseChoice / shell.cancelExit. Styled like the existing pages.
Dialog {
    id: dialog
    property bool capturing: false
    property string timeText: ""
    signal chosen(bool toTray, bool remember)
    signal cancelled()

    title: "关闭字录窗口"
    modal: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: 440
    padding: 24

    function openFor(isCapturing, time) {
        capturing = isCapturing
        timeText = time
        trayChoice.checked = true
        rememberBox.checked = true   // X3: remembered by default
        open()
    }

    background: Rectangle { color: "#2B2E38"; radius: 8; border.color: "#40454F" }
    header: Text {
        text: dialog.title; color: "#FFFFFF"; font.pixelSize: 18; font.bold: true
        leftPadding: 24; topPadding: 24
    }

    contentItem: ColumnLayout {
        spacing: 10
        Text { text: "关闭窗口后："; color: "#B0B4BC"; font.pixelSize: 14 }
        ButtonGroup { id: choiceGroup }
        RadioButton {
            id: trayChoice
            ButtonGroup.group: choiceGroup
            checked: true
            contentItem: Column {
                leftPadding: trayChoice.indicator.width + 8
                Text { text: "最小化到托盘，在后台继续运行（推荐）"; color: "#E0E0E0"; font.pixelSize: 14 }
                Text {
                    text: dialog.capturing ? "录制继续进行，计时 " + dialog.timeText
                                           : "录制不会中断，点任务栏右下角的字录图标可以重新打开"
                    color: "#808590"; font.pixelSize: 12
                }
            }
        }
        RadioButton {
            id: exitChoice
            ButtonGroup.group: choiceGroup
            contentItem: Column {
                leftPadding: exitChoice.indicator.width + 8
                Text { text: "退出字录"; color: "#E0E0E0"; font.pixelSize: 14 }
                Text { text: "正在录制时会先停止并保存"; color: "#808590"; font.pixelSize: 12 }
            }
        }
        CheckBox {
            id: rememberBox
            checked: true
            contentItem: Text {
                leftPadding: rememberBox.indicator.width + 8
                text: "记住我的选择<font color='#6A6F7A' size='2'>（可在 设置 › 常规 中修改）</font>"
                textFormat: Text.RichText; color: "#E0E0E0"; font.pixelSize: 14
                verticalAlignment: Text.AlignVCenter
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            Button { text: "取消"; onClicked: { dialog.close(); dialog.cancelled() } }
            Button {
                text: "确定"; highlighted: true
                onClicked: { dialog.close(); dialog.chosen(trayChoice.checked, rememberBox.checked) }
            }
        }
    }
    // Esc / outside close counts as cancel (same as the X icon).
    onRejected: cancelled()
}

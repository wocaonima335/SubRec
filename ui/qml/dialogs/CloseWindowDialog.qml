import QtQuick

// Module U4 (ZCode). D7 "关闭字录窗口" (UI_REDESIGN §3.6 D7, MINIMIZE_DESIGN §6.6), 440×298.
// FROZEN API: openFor(capturing, timeText) (capturing -> D7-R text "录制继续进行，计时 …");
// signals chosen(toTray, remember), cancelled(). Default: tray option, remember checked (X3).
AppDialog {
    id: root
    property bool capturing: false
    property string timeText: ""
    signal chosen(bool toTray, bool remember)
    signal cancelled()

    title: qsTr("关闭字录窗口")
    primaryText: qsTr("确定")
    secondaryText: qsTr("取消")

    function openFor(isCapturing: bool, time: string) {
        root.capturing = isCapturing
        root.timeText = time
        choiceGroup.checkedToTray = true // radio group state; tray preselected every time
        rememberBox.checked = true       // X3: remember defaults to checked
        open()
    }

    onAccepted: {
        close()
        chosen(choiceGroup.checkedToTray, rememberBox.checked)
    }
    onRejected: {
        close()
        cancelled()
    }

    QtObject {
        id: choiceGroup
        // D7 has exactly two options; a plain pair of radios sharing this state keeps the
        // dialog self-contained (ButtonGroup would live in the owner file otherwise).
        property bool checkedToTray: true
    }

    Column {
        spacing: Theme.space4

        Text {
            text: qsTr("关闭窗口后：")
            font: Theme.bodyFont
            color: Theme.textSecondary
        }

        AppRadioButton {
            text: qsTr("最小化到托盘，在后台继续运行（推荐）")
            description: root.capturing
                ? qsTr("录制继续进行，计时 %1").arg(root.timeText)
                : qsTr("录制不会中断，点任务栏右下角的字录图标可以重新打开")
            checked: choiceGroup.checkedToTray
            onClicked: choiceGroup.checkedToTray = true
        }
        AppRadioButton {
            text: qsTr("退出字录")
            description: qsTr("正在录制时会先停止并保存")
            checked: !choiceGroup.checkedToTray
            onClicked: choiceGroup.checkedToTray = false
        }

        AppCheckBox {
            id: rememberBox
            topPadding: Theme.space8
            text: qsTr("记住我的选择")
            secondaryText: qsTr("（可在 设置 › 常规 中修改）")
            checked: true
        }
    }
}

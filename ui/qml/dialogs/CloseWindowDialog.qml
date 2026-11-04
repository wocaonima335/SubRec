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

    title: "关闭字录窗口"
    primaryText: "确定"

    function openFor(isCapturing: bool, time: string) {
        root.capturing = isCapturing
        root.timeText = time
        open()
    }

    // TODO(U4): two AppRadioButtons + AppCheckBox; emit chosen(...) with the actual choice.
    onAccepted: { close(); chosen(true, true) }
    onRejected: { close(); cancelled() }
}

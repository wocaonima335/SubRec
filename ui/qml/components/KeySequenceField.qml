import QtQuick
import QtQuick.Layouts

// Module H3 (Codex). §2.5.12 KeySequenceField, 240×36, states K-默认 … K-单独功能键提示.
// FROZEN API:
//   action: HotkeyController.Action value (0 record, 1 pause, 2 window)
//   readonly message / messageKind ("" | "danger" | "warning" | "info"): line under the
//   field (owner row grows to 84 when non-empty)
// Click -> HotkeyController.beginCapture(action). Text/status come from HotkeyController
// (…Text / …Status / capturingAction / captureText); result from captureFinished.
Rectangle {
    id: root
    property int action: 0
    readonly property string message: captureMessage.length > 0 ? captureMessage : bindingStatus === "conflict" ? "已被其他程序占用，请换一个组合" : bindingStatus === "unsupported" ? "当前系统不支持全局快捷键" : ""
    readonly property string messageKind: captureMessageKind.length > 0 ? captureMessageKind : bindingStatus === "conflict" ? "danger" : bindingStatus === "unsupported" ? "info" : ""
    property string captureMessage: ""
    property string captureMessageKind: ""

    readonly property string bindingStatus: action === 0 ? HotkeyController.toggleRecordStatus : action === 1 ? HotkeyController.togglePauseStatus : HotkeyController.toggleWindowStatus
    readonly property string bindingText: action === 0 ? HotkeyController.toggleRecordText : action === 1 ? HotkeyController.togglePauseText : HotkeyController.toggleWindowText
    activeFocusOnTab: true
    Keys.onSpacePressed: begin()
    Keys.onReturnPressed: begin()
    onActiveFocusChanged: {
        if (!activeFocus && HotkeyController.capturingAction === action)
            HotkeyController.cancelCapture();
    }
    function begin() {
        forceActiveFocus();
        captureMessage = "";
        captureMessageKind = "";
        HotkeyController.beginCapture(action);
    }
    implicitWidth: 240
    implicitHeight: message.length > 0 ? 64 : 36
    radius: Theme.radiusSm
    color: fieldHover.hovered ? Theme.bgSelected : Theme.bgSurface2
    border.width: 1
    border.color: HotkeyController.capturingAction === action || activeFocus ? Theme.accentDefault : messageKind === "danger" ? Theme.dangerFg : messageKind === "warning" ? Theme.warningFg : fieldHover.hovered ? Theme.borderStrong : Theme.borderDefault

    // TODO(H3): texts, keyboard icon, capture states, messages (2 s revert for warnings).
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space8
        spacing: Theme.space4
        Text {
            font: Theme.bodyFont
            Layout.fillWidth: true
            text: HotkeyController.capturingAction === root.action ? (HotkeyController.captureText.length > 0 ? HotkeyController.captureText : "请按下组合键…") : root.bindingText.length > 0 ? root.bindingText : "未设置"
            color: Theme.textPrimary
        }
        Text {
            visible: root.message.length > 0
            text: root.message
            color: root.messageKind === "danger" ? Theme.dangerFg : root.messageKind === "warning" ? Theme.warningFg : Theme.textSecondary
            font: Theme.captionFont
        }
    }
    Text {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.space8
        visible: HotkeyController.capturingAction === root.action
        text: "Esc 取消"
        color: Theme.textTertiary
        font: Theme.captionFont
    }
    HoverHandler {
        id: fieldHover
    }
    Timer {
        id: messageTimer
        interval: Theme.blinkPeriod * 2
        onTriggered: {
            root.captureMessage = "";
            root.captureMessageKind = "";
        }
    }
    MouseArea {
        anchors.fill: parent
        onClicked: {
            if (HotkeyController.capturingAction === root.action)
                HotkeyController.cancelCapture();
            else
                root.begin();
        }
    }
    Connections {
        target: HotkeyController
        function onCaptureFinished(action, result, message) {
            if (action !== root.action)
                return;
            root.captureMessage = message;
            root.captureMessageKind = result === HotkeyController.Conflict ? "danger" : result === HotkeyController.Invalid || result === HotkeyController.Duplicate ? "warning" : "info";
            if (result === HotkeyController.Invalid || result === HotkeyController.Duplicate || result === HotkeyController.SavedWithNotice)
                messageTimer.restart();
        }
    }
}

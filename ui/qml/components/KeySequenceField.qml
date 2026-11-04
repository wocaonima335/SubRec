import QtQuick

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
    readonly property string message: ""
    readonly property string messageKind: ""

    implicitWidth: 240
    implicitHeight: 36
    radius: Theme.radiusSm
    color: Theme.bgSurface2
    border.width: 1
    border.color: Theme.borderDefault

    // TODO(H3): texts, keyboard icon, capture states, messages (2 s revert for warnings).
}

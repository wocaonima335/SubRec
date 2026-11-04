import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.3 combo box.
// FROZEN API:
//   model: JS array of { text, value, secondaryText?, enabled? } (textRole "text",
//          valueRole "value" are preset); secondaryText -> 52-px two-line option;
//          enabled === false -> disabled option with "不可用" on the right.
//   compact: 32-px trigger (P1 source panel only)
//   locked: §3.2.5 locked state (no arrow, transparent border, tooltip "录制中不能修改")
//   placeholderText: shown when currentIndex < 0 (e.g. "没有找到麦克风")
//   disabledReason: tooltip while disabled
// Select with currentIndex / indexOfValue(); react to `activated(index)` (user action only).
ComboBox {
    id: root
    property bool compact: false
    property bool locked: false
    property string placeholderText: ""
    property string disabledReason: ""

    textRole: "text"
    valueRole: "value"
    implicitHeight: compact ? Theme.compactControlHeight : Theme.controlHeight
    font: Theme.bodyFont

    // TODO(U2): popup (radius 8, max 8 rows, check mark), delegates, locked/disabled states.
    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.bgSurface2
        border.width: root.locked ? 0 : 1
        border.color: Theme.borderDefault
    }
}

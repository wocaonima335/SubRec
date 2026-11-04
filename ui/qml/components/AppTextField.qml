import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.2 text field.
// FROZEN API:
//   errorText: non-empty -> error border + 12 px message 4 px below (adds 20 px to height)
//   mono: `type/mono-sm`, elide middle when not focused (paths)
//   leadingIconName: e.g. "search" (P2), text inset becomes 36
//   locked: §3.2.5 locked state (value visible, not editable, tooltip "录制中不能修改")
//   suffixText: fixed trailing text such as ".mp4" (P3-2)
// Commit on editingFinished (focus loss / Enter), as P3-2 requires.
TextField {
    id: root
    property string errorText: ""
    property bool mono: false
    property string leadingIconName: ""
    property bool locked: false
    property string suffixText: ""

    implicitHeight: Theme.controlHeight
    readOnly: locked
    font: mono ? Theme.monoSmallFont : Theme.bodyFont
    color: Theme.textPrimary
    placeholderTextColor: Theme.textTertiary

    // TODO(U2): error line, icon, suffix, hover/focus borders, locked tooltip.
    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.bgSurface2
        border.width: 1
        border.color: root.errorText.length > 0 ? Theme.dangerFg
                    : root.activeFocus ? Theme.accentDefault : Theme.borderDefault
    }
}

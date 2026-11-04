import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.1 buttons.
// FROZEN API:
//   variant: "primary" | "secondary" (default) | "ghost" | "danger"
//   small: 28-px "Small" size (12 px text); iconName: Lucide name, "" for none
//   loading: spinner before the text, not clickable, width unchanged
//   disabledReason: tooltip shown after 500 ms hover while disabled (§2.5 "禁用")
// Use `text` and `onClicked` as with any Button.
Button {
    id: root
    property string variant: "secondary"
    property bool small: false
    property string iconName: ""
    property bool loading: false
    property string disabledReason: ""

    implicitHeight: small ? Theme.smallControlHeight : Theme.controlHeight
    font: small ? Theme.captionFont : Theme.bodyStrongFont

    // TODO(U2): background/contentItem per variant and state, focus ring, loading spinner.
    background: Rectangle {
        radius: Theme.radiusSm
        color: root.variant === "primary" ? Theme.accentDefault
             : root.variant === "danger" ? Theme.dangerFg
             : root.variant === "ghost" ? "transparent" : Theme.bgSurface2
        border.width: root.variant === "secondary" ? 1 : 0
        border.color: Theme.borderDefault
    }
}

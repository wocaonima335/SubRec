import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.6 check box 16×16, text 8 px to the right.
// FROZEN API: text, checked, secondaryText (12 px text/tertiary appended, D7 "（可在 … 修改）").
CheckBox {
    id: root
    property string secondaryText: ""
    font: Theme.bodyFont
    // TODO(U2): indicator and content item.
}

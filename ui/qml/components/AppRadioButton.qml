import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.6 radio 16×16; optional second line (P3-4 timing, D7 choices).
// FROZEN API: text, checked, description (12 px text/secondary under the text).
// Group with ButtonGroup in the owner file.
RadioButton {
    id: root
    property string description: ""
    font: Theme.bodyFont
    // TODO(U2): indicator, two-line content item.
}

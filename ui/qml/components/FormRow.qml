import QtQuick

// Module U2 (ZCode). §3.4 settings form row (shared by every P3 sub-page).
// FROZEN API:
//   title, description, descriptionKind: "normal" | "warning" (warning/fg text)
//   wide: 98-px row, control below the texts (464, +58, 768, 36); else 64-px row with the
//         control right-aligned to x=1232 (switch / 320 combo / 240 segmented / read-only text)
//   extraText + extraKind ("warning" | "danger" | "secondary"): additional 12 px line, row grows
//   showDivider: 1 px bottom divider (false for the last row of a group)
//   default property `control`: the right/below control
Item {
    id: root
    property string title: ""
    property string description: ""
    property string descriptionKind: "normal"
    property bool wide: false
    property string extraText: ""
    property string extraKind: "secondary"
    property bool showDivider: true
    default property alias control: controlSlot.data

    implicitWidth: 768
    implicitHeight: wide ? Theme.formWideRowHeight : Theme.formRowHeight

    // TODO(U2): title/description texts, control placement, extra line, divider.
    Item {
        id: controlSlot
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: childrenRect.width
        height: childrenRect.height
    }
}

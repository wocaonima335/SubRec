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
    // §3.4: 64 (98 wide) + 20 when the extra line shows.
    implicitHeight: (wide ? Theme.formWideRowHeight : Theme.formRowHeight)
                    + (extraText.length > 0 ? 20 : 0)

    readonly property color __extraColor: extraKind === "warning" ? Theme.warningFg
                                         : extraKind === "danger" ? Theme.dangerFg
                                         : Theme.textSecondary

    Text {
        id: titleText
        x: 0
        y: Theme.space12
        width: root.wide ? parent.width : 440
        text: root.title
        font: Theme.bodyStrongFont
        color: Theme.textPrimary
        elide: Text.ElideRight
    }

    Text {
        id: descText
        visible: root.description.length > 0
        x: 0
        y: Theme.space12 + 22
        width: root.wide ? parent.width : 440
        height: root.wide ? 32 : 16
        text: root.description
        font: Theme.captionFont
        color: root.descriptionKind === "warning" ? Theme.warningFg : Theme.textSecondary
        wrapMode: Text.WordWrap
            // §3.4: descriptions are one line (two for wide rows / C6's long note).
        maximumLineCount: root.wide ? 2 : 1
        elide: Text.ElideRight
    }

    // Extra line under the texts (C6 "当前屏幕采集方式下此项无效" etc.).
    Text {
        visible: root.extraText.length > 0
        x: 0
        y: root.wide ? Theme.formWideRowHeight - 2 : Theme.formRowHeight - 4
        width: root.wide ? parent.width : 440
        text: root.extraText
        font: Theme.captionFont
        color: root.__extraColor
        elide: Text.ElideRight
    }

    Item {
        id: controlSlot

        // Wide rows: the control spans below the texts; otherwise right-aligned,
        // vertically centred (§3.4).
        x: root.wide ? 0 : parent.width - childrenRect.width
        y: root.wide ? 58 : (Theme.formRowHeight - Math.max(childrenRect.height, 0)) / 2
        width: root.wide ? parent.width : Math.max(childrenRect.width, 1)
        height: root.wide ? 36 : Math.max(childrenRect.height, 1)
    }

    Rectangle {
        visible: root.showDivider
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.borderDefault
    }
}

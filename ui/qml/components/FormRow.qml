import QtQuick
import QtQuick.Layouts

// Module U2 (ZCode). §3.4 settings form row (shared by every P3 sub-page).
// FROZEN API:
//   title, description, descriptionKind: "normal" | "warning" (warning/fg text)
//   wide: 98-px row, control below the texts (464, +58, 768, 36); else 64-px row with the
//         control right-aligned to x=1232 (switch / 320 combo / 240 segmented / read-only text)
//   rowHeight: height of a non-wide row (64; taller controls such as P3-4's radio pair or a
//         hotkey field with its message set it), the control is centred in it
//   extraText + extraKind ("warning" | "danger" | "secondary"): additional 12 px line, row grows
//   showDivider: 1 px bottom divider (false for the last row of a group)
//   default property `control`: the right/below control
// Width (UI 1114, PC3 settings at 970 and below): inside a Layout the row fills the form up to
// its 768 px design width. The row sets the control's width: its implicitWidth, never more
// than the row (give a control a preferred size with implicitWidth, not width). A non-wide
// row whose texts (at least 160 px) and control do not fit side by side is `stacked`: texts
// on top, control below like a wide row. Controls elide their text instead of being cut off.
Item {
    id: root
    property string title: ""
    property string description: ""
    property string descriptionKind: "normal"
    property bool wide: false
    property real rowHeight: Theme.formRowHeight
    property string extraText: ""
    property string extraKind: "secondary"
    property bool showDivider: true
    default property alias control: controlSlot.data
    readonly property bool stacked: !wide && width > 0
                                    && width < __textMinWidth + Theme.space16 + __controlNaturalWidth

    Layout.fillWidth: true
    Layout.maximumWidth: 768
    implicitWidth: 768
    // §3.4: 64 (98 wide) + 20 when the extra line shows. A wide row also grows when its control
    // is taller than 36 (text field error line); a stacked row is texts + control + 12.
    implicitHeight: (wide ? Math.max(Theme.formWideRowHeight, __controlY + __controlHeight + Theme.space4)
                          : stacked ? __controlY + __controlHeight + Theme.space12
                          : rowHeight)
                    + (extraText.length > 0 ? 20 : 0)

    readonly property color __extraColor: extraKind === "warning" ? Theme.warningFg
                                         : extraKind === "danger" ? Theme.dangerFg
                                         : Theme.textSecondary
    readonly property Item __control: controlSlot.children.length > 0 ? controlSlot.children[0] : null
    readonly property real __controlNaturalWidth: __control ? __control.implicitWidth : 0
    readonly property real __controlHeight: __control ? __control.height : 0
    readonly property real __textMinWidth: Math.min(440, Math.max(160, titleText.implicitWidth))
    readonly property bool __below: wide || stacked
    readonly property real __controlWidth: __below ? Math.min(__controlNaturalWidth, width) : __controlNaturalWidth
    readonly property real __textWidth: __below ? width
                                                : Math.max(0, Math.min(440, width - __controlWidth - Theme.space16))
    // Below the texts: 8 px under the last text line, at least at the §3.4 +58 of a wide row
    // (+40 for a stacked row without description). A two-line wide description moves the
    // control down instead of running into it.
    readonly property real __textsBottom: description.length > 0 ? descText.y + descText.height
                                                                 : titleText.y + titleText.height
    readonly property real __controlY: wide ? Math.max(58, __textsBottom + Theme.space8)
                                     : stacked ? Math.max(40, __textsBottom + Theme.space8)
                                     : (rowHeight - __controlHeight) / 2

    Text {
        id: titleText
        objectName: "formRowTitle"
        x: 0
        y: Theme.space12
        width: root.__textWidth
        text: root.title
        font: Theme.bodyStrongFont
        color: Theme.textPrimary
        elide: Text.ElideRight
    }

    Text {
        id: descText
        objectName: "formRowDescription"
        visible: root.description.length > 0
        x: 0
        y: Theme.space12 + 22
        width: root.__textWidth
        height: root.wide ? Math.min(implicitHeight, 32) : 16
        text: root.description
        font: Theme.captionFont
        color: root.descriptionKind === "warning" ? Theme.warningFg : Theme.textSecondary
        wrapMode: Text.WordWrap
            // §3.4: descriptions are one line (two for wide rows / C6's long note).
        maximumLineCount: root.wide ? 2 : 1
        elide: Text.ElideRight
    }

    // Extra line under the texts (C6 "当前屏幕采集方式下此项无效" etc.); under the control
    // when the control sits below the texts.
    Text {
        visible: root.extraText.length > 0
        x: 0
        y: root.__below ? root.__controlY + Math.max(root.__controlHeight, root.wide ? 36 : 0) + (root.wide ? 2 : 4)
                        : root.rowHeight - 4
        width: root.__textWidth
        text: root.extraText
        font: Theme.captionFont
        color: root.__extraColor
        elide: Text.ElideRight
    }

    Item {
        id: controlSlot
        objectName: "formRowControl"

        // Wide / stacked rows: the control sits below the texts at the left; otherwise
        // right-aligned, vertically centred (§3.4).
        x: root.__below ? 0 : root.width - root.__controlWidth
        y: root.__controlY
        width: Math.max(root.__controlWidth, 1)
        height: Math.max(root.__controlHeight, 1)
    }

    // The control's width comes from the row so it can never run past the form's right edge.
    Binding {
        target: root.__control
        property: "width"
        value: root.__controlWidth
        when: root.__control !== null
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

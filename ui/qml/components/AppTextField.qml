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

    implicitHeight: Theme.controlHeight + (errorText.length > 0 ? 20 : 0)
    implicitWidth: 240
    readOnly: locked
    font: mono ? Theme.monoSmallFont : Theme.bodyFont
    color: locked ? Theme.textSecondary : Theme.textPrimary
    placeholderTextColor: Theme.textTertiary
    selectByMouse: true
    leftPadding: leadingIconName !== "" ? 36 : Theme.space12
    rightPadding: suffixText !== "" ? suffixItem.implicitWidth + Theme.space8 : Theme.space12
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter

    opacity: enabled ? 1.0 : 0.4

    // Paths elide in the middle while not being edited (§2.5.2); TextField has no elide,
    // so show the elided rendering as a overlay text while unfocused.
    Text {
        id: elideOverlay
        visible: root.mono && !root.activeFocus && root.text.length > 0
        anchors.fill: parent
        anchors.leftMargin: root.leftPadding
        anchors.rightMargin: root.rightPadding
        verticalAlignment: Text.AlignVCenter
        font: root.font
        color: root.color
        text: root.text
        elide: Text.ElideMiddle
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: root.locked ? Theme.bgApp : Theme.bgSurface2
        border.width: 1
        border.color: root.errorText.length > 0 ? Theme.dangerFg
                    : !root.locked && root.activeFocus ? Theme.accentDefault
                    : !root.locked && root.hovered ? Theme.borderStrong
                    : Theme.borderDefault
    }

    AppIcon {
        visible: root.leadingIconName.length > 0
        anchors.left: parent.left
        anchors.leftMargin: Theme.space12
        anchors.verticalCenter: parent.verticalCenter
        name: root.leadingIconName
        size: 16
        color: Theme.textTertiary
    }

    Text {
        id: suffixItem
        visible: root.suffixText.length > 0
        anchors.right: parent.right
        anchors.rightMargin: Theme.space12
        anchors.verticalCenter: parent.verticalCenter
        text: root.suffixText
        font: Theme.bodyFont
        color: Theme.textTertiary
    }

    // Error message 4 px below the 36-px field (§2.5.2); the control grows by 20 px.
    Text {
        id: errorLine
        visible: root.errorText.length > 0
        y: Theme.controlHeight + Theme.space4
        anchors.left: parent.left
        width: root.width
        text: root.errorText
        font: Theme.captionFont
        color: Theme.dangerFg
        elide: Text.ElideRight
    }

    HoverHandler {
        id: hover
    }
    AppToolTip {
        visible: root.locked && hover.hovered
        text: qsTr("录制中不能修改")
    }
}

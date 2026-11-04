import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.6 check box 16×16, text 8 px to the right.
// FROZEN API: text, checked, secondaryText (12 px text/tertiary appended, D7 "（可在 … 修改）").
CheckBox {
    id: root
    property string secondaryText: ""
    font: Theme.bodyFont

    spacing: Theme.space8
    implicitWidth: indicator.width + spacing + contentRow.implicitWidth
    implicitHeight: Math.max(20, contentRow.implicitHeight)
    opacity: enabled ? 1.0 : 0.4

    indicator: Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        y: (root.height - height) / 2
        radius: Theme.radiusXs
        color: root.checked ? Theme.accentDefault
             : root.hovered && !root.checked ? Theme.bgSurface2 : "transparent"
        border.width: root.checked ? 0 : 1.5
        border.color: root.hovered && !root.checked ? Theme.accentDefault : Theme.borderStrong

        AppIcon {
            anchors.centerIn: parent
            visible: root.checked
            name: "check"
            size: 12
            color: Theme.textOnAccent
        }
    }

    contentItem: Row {
        id: contentRow
        spacing: 0

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font: root.font
            color: Theme.textPrimary
        }
        Text {
            visible: root.secondaryText.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: root.secondaryText.length > 0 ? " " + root.secondaryText : ""
            font: Theme.captionFont
            color: Theme.textTertiary
        }
    }
}

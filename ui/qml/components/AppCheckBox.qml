import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.6 check box 16×16, text 8 px to the right.
// FROZEN API: text, checked, secondaryText (12 px text/tertiary appended, D7 "（可在 … 修改）").
// Geometry as in the Basic CheckBox template: the control itself puts contentItem at
// (leftPadding, topPadding), so the text clears the indicator through the content's own
// left/right padding (mirrored aware).
CheckBox {
    id: root
    property string secondaryText: ""
    font: Theme.bodyFont

    padding: 0
    spacing: Theme.space8
    implicitWidth: Math.max(implicitContentWidth, implicitIndicatorWidth) + leftPadding + rightPadding
    implicitHeight: Math.max(20, implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    opacity: enabled ? 1.0 : 0.4

    indicator: Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        x: root.text.length > 0
           ? (root.mirrored ? root.width - width - root.rightPadding : root.leftPadding)
           : root.leftPadding + (root.availableWidth - width) / 2
        // Within the padded area, so a caller's topPadding (D7) keeps box and text level.
        y: root.topPadding + (root.availableHeight - height) / 2
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
        leftPadding: root.text.length > 0 && !root.mirrored ? root.indicator.width + root.spacing : 0
        rightPadding: root.text.length > 0 && root.mirrored ? root.indicator.width + root.spacing : 0
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

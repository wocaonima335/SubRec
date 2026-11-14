import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.6 radio 16×16; optional second line (P3-4 timing, D7 choices).
// FROZEN API: text, checked, description (12 px text/secondary under the text).
// Group with ButtonGroup in the owner file.
// Geometry as in the Basic RadioButton template: the control itself puts contentItem at
// (leftPadding, topPadding), so the text clears the indicator through the content's own
// left/right padding (mirrored aware), never through contentItem.x.
RadioButton {
    id: root
    property string description: ""
    font: Theme.bodyFont

    padding: 0
    spacing: Theme.space8
    implicitWidth: Math.max(implicitContentWidth, implicitIndicatorWidth) + leftPadding + rightPadding
    implicitHeight: Math.max(description.length > 0 ? 40 : 20,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    opacity: enabled ? 1.0 : 0.4

    indicator: Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        x: root.text.length > 0
           ? (root.mirrored ? root.width - width - root.rightPadding : root.leftPadding)
           : root.leftPadding + (root.availableWidth - width) / 2
        // Centred on the first text line; the description hangs below it. A line shorter than
        // the dot moves the text down (contentItem.topPadding), never the dot above the control.
        y: root.text.length > 0 ? root.topPadding + Math.max(0, (titleText.height - height) / 2)
                                : root.topPadding + (root.availableHeight - height) / 2
        radius: 8
        color: "transparent"
        border.width: 1.5
        border.color: root.checked ? Theme.accentDefault
                    : root.hovered && !root.checked ? Theme.accentDefault : Theme.borderStrong

        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 8
            radius: 4
            visible: root.checked
            color: Theme.accentDefault
        }
    }

    contentItem: Column {
        // A text-less radio (ModelRow) is only the 16-px dot: nothing to reserve.
        leftPadding: root.text.length > 0 && !root.mirrored ? root.indicator.width + root.spacing : 0
        rightPadding: root.text.length > 0 && root.mirrored ? root.indicator.width + root.spacing : 0
        topPadding: root.text.length > 0 ? Math.max(0, (root.implicitIndicatorHeight - titleText.height) / 2) : 0
        spacing: 0

        Text {
            id: titleText
            text: root.text
            font: root.font
            color: Theme.textPrimary
        }
        Text {
            visible: root.description.length > 0
            text: root.description
            font: Theme.captionFont
            color: Theme.textSecondary
        }
    }
}

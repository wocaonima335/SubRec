import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.6 radio 16×16; optional second line (P3-4 timing, D7 choices).
// FROZEN API: text, checked, description (12 px text/secondary under the text).
// Group with ButtonGroup in the owner file.
RadioButton {
    id: root
    property string description: ""
    font: Theme.bodyFont

    spacing: Theme.space8
    implicitWidth: Math.max(indicator.width + spacing + contentItem.implicitWidth, 0)
    implicitHeight: description.length > 0 ? 40 : 20
    opacity: enabled ? 1.0 : 0.4

    indicator: Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        x: 0
        y: Theme.space4
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
        x: root.indicator.width + root.spacing
        spacing: 0

        Text {
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

import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.11 nav item 176×40 (settings sub-nav: `dense` 176×36, icon 16).
// FROZEN API:
//   iconName, text, selected (left 3×16 indicator, bg/selected, accent/text)
//   collapsed: icon only (narrow window, §2.7), tooltip shows `text`
//   dotColor: 8 px dot at the right ("录制" item while capturing); transparent = none
//   dense: settings sub-navigation size
AbstractButton {
    id: root
    property string iconName: ""
    // `text` comes from AbstractButton.
    property bool selected: false
    property bool collapsed: false
    property color dotColor: "transparent"
    property bool dense: false

    implicitWidth: collapsed ? 40 : 176
    implicitHeight: dense ? 36 : Theme.navItemHeight
    hoverEnabled: true
    font: selected ? Theme.bodyStrongFont : Theme.bodyFont

    readonly property int __iconSize: dense ? 16 : 20
    readonly property bool __hasDot: dotColor.a > 0

    background: Rectangle {
        radius: Theme.radiusSm
        color: root.selected ? Theme.bgSelected : root.hovered ? Theme.bgHover : "transparent"

        // §2.5.11: 3×16 indicator bar hugging the left edge.
        Rectangle {
            visible: root.selected
            x: 0
            y: (parent.height - height) / 2
            width: 3
            height: 16
            radius: 1.5
            color: Theme.accentDefault
        }
    }

    contentItem: Item {
        implicitWidth: root.collapsed ? root.__iconSize
                                      : root.__iconSize + Theme.space12 + label.implicitWidth

        AppIcon {
            id: navIcon
            x: root.collapsed ? (root.width - width) / 2 : Theme.space12
            anchors.verticalCenter: parent.verticalCenter
            name: root.iconName
            size: root.__iconSize
            color: root.selected ? Theme.accentText : root.hovered ? Theme.textPrimary : Theme.textSecondary
        }

        Text {
            id: label
            visible: !root.collapsed
            anchors.left: parent.left
            anchors.leftMargin: Theme.space12 + root.__iconSize + Theme.space12
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font: root.font
            color: root.selected ? Theme.accentText : root.hovered ? Theme.textPrimary : Theme.textSecondary
        }

        // Recording dot (§3.1): 8 px at the right, 12 px from the edge.
        Rectangle {
            visible: !root.collapsed && root.__hasDot
            anchors.right: parent.right
            anchors.rightMargin: Theme.space12
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: root.dotColor
        }
        Rectangle {
            visible: root.collapsed && root.__hasDot
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 4
            width: 8
            height: 8
            radius: 4
            color: root.dotColor
        }
    }

    HoverHandler {
        id: hover
    }
    AppToolTip {
        visible: root.collapsed && hover.hovered
        text: root.text
    }
}

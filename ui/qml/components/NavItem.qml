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
    property bool selected: false
    property bool collapsed: false
    property color dotColor: "transparent"
    property bool dense: false

    implicitWidth: collapsed ? 40 : 176
    implicitHeight: dense ? 36 : Theme.navItemHeight
    hoverEnabled: true
    font: selected ? Theme.bodyStrongFont : Theme.bodyFont

    // TODO(U2): icon + text layout, indicator bar, dot, collapsed tooltip.
    background: Rectangle {
        radius: Theme.radiusSm
        color: root.selected ? Theme.bgSelected : root.hovered ? Theme.bgHover : "transparent"
    }
}

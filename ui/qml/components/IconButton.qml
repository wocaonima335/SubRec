import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.1 "图标按钮 Icon" 32×32; also the 46×40 window buttons and the
// round floating-bar buttons.
// FROZEN API:
//   iconName, iconSize (16), toolTipText (500 ms delay)
//   round: circle background (floating bar); dangerHover: hover icon/background danger colours
//   (P2 delete, FB stop); hoverColor: override, e.g. Theme.closeButtonHover for the title bar.
AbstractButton {
    id: root
    property string iconName: ""
    property int iconSize: 16
    property string toolTipText: ""
    property bool round: false
    property bool dangerHover: false
    property color hoverColor: Theme.bgHover

    implicitWidth: 32
    implicitHeight: 32
    hoverEnabled: true

    // TODO(U2): states, tooltip, focus ring.
    background: Rectangle {
        radius: root.round ? width / 2 : Theme.radiusSm
        color: root.hovered ? root.hoverColor : "transparent"
    }
    contentItem: AppIcon {
        name: root.iconName
        size: root.iconSize
        color: root.hovered ? Theme.textPrimary : Theme.textSecondary
    }
}

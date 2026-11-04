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
    // 46×40 window caption buttons (§3.1); 32×32 everywhere else.
    property bool windowButton: false

    implicitWidth: windowButton ? 46 : 32
    implicitHeight: windowButton ? 40 : 32
    hoverEnabled: enabled

    opacity: enabled ? 1.0 : 0.4

    background: Rectangle {
        radius: root.round ? width / 2 : Theme.radiusSm
        color: root.pressed ? (root.dangerHover ? Theme.dangerBg : Theme.bgPressed)
             : root.hovered ? root.hoverColor
             : "transparent"
    }

    contentItem: AppIcon {
        name: root.iconName
        size: root.iconSize
        color: root.dangerHover && root.hovered ? Theme.dangerFg
             : root.hovered ? Theme.textPrimary
             : Theme.textSecondary
    }

    HoverHandler {
        id: hover
    }
    AppToolTip {
        visible: hover.hovered && root.toolTipText.length > 0 && root.enabled
        text: root.toolTipText
    }

    Rectangle {
        visible: root.activeFocus && root.focusReason === Qt.TabFocusReason
        anchors.fill: parent
        anchors.margins: -Theme.space4
        radius: (root.round ? width / 2 : Theme.radiusSm) + 2
        color: "transparent"
        border.width: 2
        border.color: Theme.focusRing
        z: -1
    }
}

import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.1 "图标按钮 Icon" 32×32; also the 46×40 window buttons and the
// round floating-bar buttons.
// FROZEN API:
//   iconName, iconSize (16), toolTipText (500 ms delay)
//   round: circle background (floating bar); dangerHover: hover icon/background danger colours
//   (P2 delete, FB stop); hoverColor: override, e.g. Theme.closeButtonHover for the title bar.
//   captionKind: "" (default) shows the AppIcon `iconName`; "minimize" | "maximize" |
//   "restore" | "close" shows a CaptionGlyph instead (title bar, 16 px, centred on whole
//   logical px), same colours.
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
    property string captionKind: ""

    readonly property color __contentColor: root.dangerHover && root.hovered ? Theme.dangerFg
                                          : root.hovered ? Theme.textPrimary
                                          : Theme.textSecondary

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

    // The AppIcon keeps the geometry it had as the contentItem itself (fills the item).
    contentItem: Item {
        implicitWidth: root.iconSize
        implicitHeight: root.iconSize
        AppIcon {
            objectName: "iconButtonIcon"
            anchors.fill: parent
            visible: root.captionKind.length === 0
            name: root.captionKind.length === 0 ? root.iconName : ""
            size: root.iconSize
            color: root.__contentColor
        }
        Loader {
            objectName: "captionGlyphLoader"
            // Implicit 16 × 16; alignWhenCentered keeps the position on whole logical px.
            anchors.centerIn: parent
            active: root.captionKind.length > 0
            sourceComponent: CaptionGlyph {
                objectName: "captionGlyph"
                kind: root.captionKind
                color: root.__contentColor
            }
        }
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

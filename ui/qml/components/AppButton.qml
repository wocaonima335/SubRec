import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.1 buttons.
// FROZEN API:
//   variant: "primary" | "secondary" (default) | "ghost" | "danger"
//   small: 28-px "Small" size (12 px text); iconName: icon name from ui/icons/ICONS_SOURCE.md, "" for none
//   loading: spinner before the text, not clickable, width unchanged
//   disabledReason: tooltip shown after 500 ms hover while disabled (§2.5 "禁用")
// Use `text` and `onClicked` as with any Button.
Button {
    id: root
    property string variant: "secondary"
    property bool small: false
    property string iconName: ""
    property bool loading: false
    property string disabledReason: ""

    readonly property bool __ghost: variant === "ghost"
    readonly property bool __primary: variant === "primary"
    readonly property bool __danger: variant === "danger"

    implicitHeight: small ? Theme.smallControlHeight : Theme.controlHeight
    leftPadding: small ? Theme.space12 : Theme.space16
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: 0
    // §2.5.1: min width 88 (64 small).
    implicitWidth: Math.max(small ? 64 : 88,
                            contentItem.implicitWidth + leftPadding + rightPadding)
    // 12 px SemiBold small / 14 px SemiBold normal (sizes from Theme tokens).
    font.family: Theme.fontFamily
    font.pixelSize: small ? Theme.captionFont.pixelSize : Theme.bodyStrongFont.pixelSize
    font.weight: Font.Bold

    // §2.5 "禁用": whole control 40 % opacity, default arrow cursor.
    opacity: enabled ? 1.0 : 0.4
    hoverEnabled: enabled && !loading
    // Loading state keeps the width: only interactions are blocked (§2.5.1).
    icon.width: 0
    icon.height: 0
    spacing: 0

    onLoadingChanged: if (!loading) clickGuard.visible = false

    background: Rectangle {
        radius: Theme.radiusSm
        color: {
            if (root.__primary)
                return root.pressed ? Theme.accentPressed : root.hovered ? Theme.accentHover : Theme.accentDefault
            if (root.__danger)
                return root.pressed ? Theme.dangerFg : root.hovered ? Theme.recordHover : Theme.dangerFg
            if (root.__ghost)
                return root.pressed ? Theme.bgPressed : root.hovered ? Theme.bgHover : "transparent"
            return root.pressed ? Theme.bgPressed : root.hovered ? Theme.bgHover : Theme.bgSurface2
        }
        border.width: root.variant === "secondary" ? 1 : 0
        border.color: root.hovered ? Theme.borderStrong : Theme.borderDefault
    }

    contentItem: Row {
        spacing: root.iconName !== "" || root.loading ? Theme.space8 : 0
        leftPadding: 0

        Spinner {
            visible: root.loading
            anchors.verticalCenter: parent.verticalCenter
            size: 16
            color: root.__ghost || root.variant === "secondary"
                ? Theme.textPrimary : Theme.textOnAccent
        }

        AppIcon {
            visible: root.iconName !== "" && !root.loading
            anchors.verticalCenter: parent.verticalCenter
            name: root.iconName
            size: 16
            color: root.__ghost || root.variant === "secondary"
                ? (root.hovered ? Theme.textPrimary : Theme.textSecondary)
                : Theme.textOnAccent
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font: root.font
            color: root.__primary || root.__danger ? Theme.textOnAccent
                 : root.__ghost ? Theme.accentText
                 : root.hovered ? Theme.textPrimary : Theme.textPrimary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // Loading blocks clicks without changing width (§2.5.1): an invisible guard eats them.
    MouseArea {
        id: clickGuard
        anchors.fill: parent
        enabled: root.loading
        visible: root.loading
    }

    HoverHandler {
        id: disabledHover
        enabled: !root.enabled
    }
    AppToolTip {
        visible: disabledHover.hovered && root.disabledReason.length > 0
        text: root.disabledReason
    }

    // Keyboard focus ring (§2.5 "焦点"): 2 px outside a 2 px gap, only on key navigation.
    Rectangle {
        visible: root.activeFocus && root.focusReason === Qt.TabFocusReason
        anchors.fill: parent
        anchors.margins: -Theme.space4
        radius: Theme.radiusSm + 2
        color: "transparent"
        border.width: 2
        border.color: Theme.focusRing
        z: -1
    }
}

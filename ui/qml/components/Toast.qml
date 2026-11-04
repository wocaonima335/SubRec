pragma ComponentBehavior: Bound
import QtQuick

// Module U2 (ZCode). One toast, 360 wide (§3.7 layout: icon, title, close, body, actions).
// FROZEN API: kind, title, body, actions (as Toasts.show); signals actionClicked(id), dismissed().
// durationMs (added, see Toasts.show): 0 = default (5 s, 8 s with actions).
Rectangle {
    id: root
    property string kind: "info"
    property string title: ""
    property string body: ""
    property var actions: []
    property int durationMs: 0
    signal actionClicked(string actionId)
    signal dismissed()

    readonly property color __fg: kind === "danger" ? Theme.dangerFg
                                 : kind === "warning" ? Theme.warningFg
                                 : kind === "success" ? Theme.successFg : Theme.infoFg
    readonly property string __iconName: kind === "danger" ? "circle-alert"
                                       : kind === "warning" ? "triangle-alert"
                                       : kind === "success" ? "circle-check" : "info"

    // §3.7: 104 with actions, 70 with body, 52 title only.
    width: 360
    implicitHeight: actions.length > 0 ? 104 : body.length > 0 ? 70 : 52
    radius: Theme.radiusMd
    color: Theme.bgSurface
    border.width: 1
    border.color: Theme.borderDefault

    // Auto-dismiss (§2.5.10); the timer pauses while hovered.
    Timer {
        id: lifeTimer
        interval: root.durationMs > 0 ? root.durationMs
                : root.actions.length > 0 ? 8000 : 5000
        running: !hover.hovered
        onTriggered: root.dismissed()
    }

    HoverHandler {
        id: hover
    }

    AppIcon {
        x: Theme.space16
        y: Theme.space16
        name: root.__iconName
        size: 16
        color: root.__fg
    }

    Text {
        x: 44
        y: 14
        width: 316 - 44 - 36
        text: root.title
        font: Theme.bodyStrongFont
        color: Theme.textPrimary
        elide: Text.ElideRight
    }

    IconButton {
        x: 316
        y: 12
        width: 32
        height: 32
        iconName: "x"
        iconSize: 16
        onClicked: root.dismissed()
    }

    Text {
        visible: root.body.length > 0
        x: 44
        y: 38
        width: 360 - 44 - Theme.space16
        text: root.body
        font: Theme.captionFont
        color: Theme.textSecondary
        elide: Text.ElideRight
    }

    Row {
        visible: root.actions.length > 0
        x: 44
        y: 62
        spacing: Theme.space8

        Repeater {
            model: root.actions

            delegate: AppButton {
                required property var modelData
                small: true
                variant: modelData.ghost === true ? "ghost" : "secondary"
                text: modelData.text
                onClicked: root.actionClicked(modelData.id)
            }
        }
    }
}

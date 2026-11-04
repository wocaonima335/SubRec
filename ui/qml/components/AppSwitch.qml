import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.4 switch, 36×20 track, no text (label lives in the row).
// FROZEN API: checked, locked (§3.2.5), disabledReason; react to `toggled()` (user action).
// Bindings: owners must re-assert `checked` from the backend after toggled (Basic Switch
// breaks a declarative binding on user toggle; see review S7). U2 handles this internally
// by exposing `backendChecked`: bind that instead of `checked` when the value lives in C++.
// AbstractButton base (checkable) instead of Switch: Switch's `handle` is style-internal,
// so the knob is drawn inside `background` here; toggled()/checked semantics are the same.
AbstractButton {
    id: root
    property bool locked: false
    property string disabledReason: ""
    property bool backendChecked: false

    implicitWidth: 36
    implicitHeight: 20
    checkable: true
    checked: backendChecked

    // A user toggle breaks the `checked` binding; re-establish it so the knob always
    // shows the backend value (a rejected write visibly snaps back). The owner writes the
    // backend from its own onToggled, which runs after this one.
    onToggled: checked = Qt.binding(function() { return root.backendChecked })

    hoverEnabled: enabled && !locked
    opacity: enabled ? 1.0 : 0.4

    background: Rectangle {
        implicitWidth: 36
        implicitHeight: 20
        radius: 10
        color: root.checked ? (root.pressed ? Theme.accentPressed
                                : root.hovered ? Theme.accentHover : Theme.accentDefault)
              : Theme.bgSurface2
        border.width: root.checked ? 0 : 1
        border.color: root.hovered && !root.checked ? Theme.accentDefault : Theme.borderStrong

        // §2.5.4: knob 16×16 white, 2 px from the track edge, 150 ms OutCubic.
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            x: 2 + (root.checked ? parent.width - 4 - width : 0)
            width: 16
            height: 16
            radius: 8
            color: "#FFFFFF"

            Behavior on x {
                enabled: !Theme.reduceMotion
                NumberAnimation { duration: Theme.durationToggle; easing.type: Easing.OutCubic }
            }
        }
    }

    HoverHandler {
        id: hover
    }
    AppToolTip {
        visible: root.locked && hover.hovered
        text: qsTr("录制中不能修改")
    }
    AppToolTip {
        visible: !root.enabled && hover.hovered && root.disabledReason.length > 0
        text: root.disabledReason
    }
}

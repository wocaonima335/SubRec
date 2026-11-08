import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.4 switch, 36×20 track, no text (label lives in the row).
// FROZEN API: checked, locked (§3.2.5), disabledReason, backendChecked;
//   signal toggledTo(bool value): user action, `value` is the state the user asked for.
//   Owners write the backend from onToggledTo(value). (`toggled()` still exists, but
//   `checked` inside an owner's onToggled is not the new state - see below.)
// Bindings: bind `backendChecked` (not `checked`) when the value lives in C++; the knob
// always shows backendChecked, so a rejected write visibly snaps back.
// AbstractButton base (checkable) instead of Switch: Switch's `handle` is style-internal,
// so the knob is drawn inside `background` here.
AbstractButton {
    id: root
    property bool locked: false
    property string disabledReason: ""
    property bool backendChecked: false
    signal toggledTo(bool value)

    implicitWidth: 36
    implicitHeight: 20
    checkable: true
    checked: backendChecked

    // A user toggle breaks the `checked` binding. Capture the requested state first, then
    // re-bind to the backend, then hand the requested state to the owner. Owners that read
    // `checked` in their own onToggled got the re-bound (old) value whenever this handler
    // ran first - the order of the two handlers is not something to rely on - so the value
    // is passed explicitly instead. Locked switches report nothing (§3.2.5).
    onToggled: {
        const requested = root.checked
        root.checked = Qt.binding(function() { return root.backendChecked })
        if (!root.locked)
            root.toggledTo(requested)
    }

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

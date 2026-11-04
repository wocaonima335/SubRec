import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.4 switch, 36×20 track, no text (label lives in the row).
// FROZEN API: checked, locked (§3.2.5), disabledReason; react to `toggled()` (user action).
// Bindings: owners must re-assert `checked` from the backend after toggled (Basic Switch
// breaks a declarative binding on user toggle; see review S7). U2 handles this internally
// by exposing `backendChecked`: bind that instead of `checked` when the value lives in C++.
Switch {
    id: root
    property bool locked: false
    property string disabledReason: ""
    property bool backendChecked: false

    implicitWidth: 36
    implicitHeight: 20
    checked: backendChecked

    // TODO(U2): track/knob drawing, 150 ms OutCubic, locked/disabled tooltips,
    // re-sync checked <- backendChecked after every toggle.
}

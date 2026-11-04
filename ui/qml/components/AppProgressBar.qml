import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.6 progress bar, 4 px; `indeterminate` -> 30 % block, 1.2 s loop.
// FROZEN API: value (0..1, from/to defaults), indeterminate.
ProgressBar {
    id: root
    implicitHeight: 4
    // TODO(U2): track/fill/indeterminate animation.
}

import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.6 progress bar, 4 px; `indeterminate` -> 30 % block, 1.2 s loop.
// FROZEN API: value (0..1, from/to defaults), indeterminate.
ProgressBar {
    id: root
    implicitHeight: 4
    implicitWidth: 200

    background: Rectangle {
        radius: 2
        color: Theme.borderDefault
    }

    contentItem: Item {
        clip: true

        // Determinate: fill grows with the value.
        Rectangle {
            visible: !root.indeterminate
            width: parent.width * root.position
            height: parent.height
            radius: 2
            color: Theme.accentDefault
        }

        // Indeterminate: 30 % block loops across the track in 1.2 s (§2.5.6).
        Rectangle {
            id: indetBlock
            visible: root.indeterminate
            width: parent.width * 0.3
            height: parent.height
            radius: 2
            color: Theme.accentDefault
            x: -width

            NumberAnimation on x {
                running: root.indeterminate && root.visible && !Theme.reduceMotion
                from: -indetBlock.width
                to: root.width
                duration: 1200
                loops: Animation.Infinite
            }
        }
    }
}

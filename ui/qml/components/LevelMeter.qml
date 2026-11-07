import QtQuick

// Module U2 (ZCode). §2.5.9 level meter, 6 high, radius 3.
// FROZEN API:
//   levelDb: dBFS (AudioLevelMonitor), segments <-12 success, -12..-3 warning, >-3 danger
//   active: false -> empty track only (idle / no data)
//   channelOn: false -> track uses bg/surface-2, no fill
// Peak hold (2×6 marker, hold 1 s, fall 20 dB/s) is internal.
Item {
    id: root
    property real levelDb: -60
    property bool active: false
    property bool channelOn: true

    implicitHeight: 6
    implicitWidth: 248

    // §2.5.9 scale: -60..0 dBFS maps to 0..1; boundaries -12 and -3 dB.
    readonly property real __frac: active && channelOn
        ? Math.max(0, Math.min(1, (levelDb + 60) / 60)) : 0

    Rectangle {
        anchors.fill: parent
        radius: 3
        color: root.channelOn ? Theme.borderDefault : Theme.bgSurface2
    }

    Item {
        anchors.fill: parent
        clip: true
        visible: root.channelOn

        // Three coloured segments, each clipped to the current level fraction.
        Rectangle { // -60..-12 dB -> success
            y: 0; height: parent.height; radius: 2
            x: 0
            width: root.width * Math.max(0, Math.min(root.__frac, 48 / 60))
            color: Theme.successFg
        }
        Rectangle { // -12..-3 dB -> warning
            y: 0; height: parent.height; radius: 2
            x: root.width * (48 / 60)
            width: root.width * Math.max(0, Math.min(root.__frac, 57 / 60) - 48 / 60)
            color: Theme.warningFg
        }
        Rectangle { // -3..0 dB -> danger
            y: 0; height: parent.height; radius: 2
            x: root.width * (57 / 60)
            width: root.width * Math.max(0, root.__frac - 57 / 60)
            color: Theme.dangerFg
        }

        // Peak-hold marker (§2.5.9): 2×6 line, held 1 s, then falls at 20 dB/s.
        Rectangle {
            id: peakMarker
            x: root.width * root.peakFrac - 1
            width: 2
            height: parent.height
            color: Theme.textPrimary
            visible: root.channelOn && root.active && root.peakFrac > 0
        }
    }

    property real peakFrac: 0

    Timer {
        // 10 ticks/s: comfortably within the ≤30 Hz refresh budget of §2.5.9.
        id: peakTimer
        interval: 100
        running: root.visible && root.active
        repeat: true
        property real lastPeakAt: 0
        onTriggered: {
            const now = Date.now()
            if (root.__frac >= root.peakFrac) {
                root.peakFrac = root.__frac
                peakTimer.lastPeakAt = now
            } else if (now - peakTimer.lastPeakAt > 1000) {
                // Fall 20 dB/s = 1/3 of the scale per second.
                root.peakFrac = Math.max(root.__frac, root.peakFrac - (20 / 60) * 0.1)
            }
        }
    }
}

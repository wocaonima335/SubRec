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

    // TODO(U2): fill, colours, peak marker, <= 30 Hz repaint.
    Rectangle {
        anchors.fill: parent
        radius: 3
        color: root.channelOn ? Theme.borderDefault : Theme.bgSurface2
    }
}

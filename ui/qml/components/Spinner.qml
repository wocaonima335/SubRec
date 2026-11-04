import QtQuick

// Module U2 (ZCode). Rotating ring, 2 px stroke (§2.5.1 loading state, §2.5.8, FB-1/FB-4).
// FROZEN API: size, color, running.
// TODO(U2): arc drawing (Shape or Canvas) + RotationAnimator; static when Theme.reduceMotion.
Item {
    id: root
    property int size: 16
    property color color: Theme.textPrimary
    property bool running: visible

    implicitWidth: size
    implicitHeight: size
}

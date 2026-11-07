import QtQuick

// Module U2 (ZCode). Rotating ring, 2 px stroke (§2.5.1 loading state, §2.5.8, FB-1/FB-4).
// FROZEN API: size, color, running.
// Static quarter arc when Theme.reduceMotion (§2.6: the glyph still reads as "busy").
Item {
    id: root
    property int size: 16
    property color color: Theme.textPrimary
    property bool running: visible

    implicitWidth: size
    implicitHeight: size

    onSizeChanged: ring.requestPaint()

    Canvas {
        id: ring
        anchors.fill: parent

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = root.color
            ctx.lineWidth = 2
            ctx.lineCap = "round"
            const r = Math.max(1, root.size / 2 - 1)
            ctx.beginPath()
            ctx.arc(root.size / 2, root.size / 2, r, 0, Math.PI * 1.5)
            ctx.stroke()
        }

        Component.onCompleted: requestPaint()

        Connections {
            target: root
            function onColorChanged() { ring.requestPaint() }
        }

        RotationAnimator {
            target: ring
            from: 0
            to: 360
            duration: 1000
            loops: Animation.Infinite
            running: root.running && !Theme.reduceMotion
        }
    }
}

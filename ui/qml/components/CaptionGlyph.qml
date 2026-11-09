import QtQuick
import QtQuick.Shapes
import QtQuick.Window

// Module U4 (ZCode). §3.1 window caption glyph for the title bar buttons, Windows 11 style:
// minimize (line), maximize (box), restore (two boxes), close (thin X), about 10 logical px
// inside a 16 x 16 item. Drawn as Shape strokes on the device-pixel grid instead of an SVG
// through AppIcon (titlebar-icon-diag-0834 option 2): the stroke is a whole number of device
// pixels at every scale, so it stays a solid 1 px line at 100 / 125 % instead of a blurred
// 1.25 px fill. No MultiEffect; `color` goes straight to the stroke.
// API: kind ("minimize" | "maximize" | "restore" | "close"), color.
// Read-only helpers (tests): strokePhysical, strokeLogical, polylines.
// devicePixelRatio follows Screen (re-evaluated when the window changes screens); tests set it.
Item {
    id: root
    property string kind: ""
    property color color: Theme.textSecondary
    // Screen.devicePixelRatio is 0 until the item is in a window, hence the floor of 1.
    property real devicePixelRatio: Math.max(1, Screen.devicePixelRatio)

    readonly property int iconLogical: 16
    readonly property real glyphLogical: 10

    // Formulas and operation order must match ui/backend/caption_glyph_geometry.h
    // (physicalStrokePx, logicalStroke, snapToPhysicalEdge, snapToPhysicalCenter,
    // glyphPolylines); ui_controls_tests compares them.
    readonly property int strokePhysical: Math.max(1, Math.round(devicePixelRatio))
    readonly property real strokeLogical: strokePhysical / devicePixelRatio

    function snapToPhysicalEdge(logical: real): real {
        return Math.round(logical * root.devicePixelRatio) / root.devicePixelRatio
    }
    function snapToPhysicalCenter(logical: real): real {
        const half = root.strokePhysical / 2.0
        return (Math.round(logical * root.devicePixelRatio - half) + half) / root.devicePixelRatio
    }

    // List of polylines (lists of points, logical px); see glyphPolylines in the header.
    readonly property var polylines: {
        const half = root.strokeLogical / 2
        const a = root.snapToPhysicalEdge((root.iconLogical - root.glyphLogical) / 2)
        const b = root.snapToPhysicalEdge((root.iconLogical + root.glyphLogical) / 2)
        const lo = a + half
        const hi = b - half
        if (root.kind === "minimize") {
            const y = root.snapToPhysicalCenter(root.iconLogical / 2)
            return [[Qt.point(a, y), Qt.point(b, y)]]
        }
        if (root.kind === "maximize") {
            const mid = root.snapToPhysicalEdge(root.iconLogical / 2)
            return [[Qt.point(mid, lo), Qt.point(hi, lo), Qt.point(hi, hi), Qt.point(lo, hi),
                     Qt.point(lo, lo), Qt.point(mid, lo)]]
        }
        if (root.kind === "restore") {
            const d = Math.max(root.snapToPhysicalEdge(2), (root.strokePhysical + 1) / root.devicePixelRatio)
            const frontRight = b - d - half
            const frontTop = a + d + half
            const mid = root.snapToPhysicalEdge((a + b - d) / 2)
            return [[Qt.point(mid, frontTop), Qt.point(frontRight, frontTop), Qt.point(frontRight, hi),
                     Qt.point(lo, hi), Qt.point(lo, frontTop), Qt.point(mid, frontTop)],
                    [Qt.point(a + d + half, a + d), Qt.point(a + d + half, lo), Qt.point(hi, lo),
                     Qt.point(hi, b - d - half), Qt.point(b - d, b - d - half)]]
        }
        if (root.kind === "close")
            return [[Qt.point(lo, lo), Qt.point(hi, hi)], [Qt.point(hi, lo), Qt.point(lo, hi)]]
        return []
    }

    implicitWidth: iconLogical
    implicitHeight: iconLogical

    Shape {
        objectName: "captionGlyphShape"
        // Whole logical px inside the item even if a parent resizes it.
        x: Math.floor((root.width - root.iconLogical) / 2)
        y: Math.floor((root.height - root.iconLogical) / 2)
        width: root.iconLogical
        height: root.iconLogical
        // Plain triangulated geometry, no anti-aliasing: the snapped straight strokes cover
        // whole device pixels, so they come out solid. Only the close X has diagonals; it gets
        // 4x multisampling in a device-pixel layer (the documented way to anti-alias this
        // renderer) so its edges are smooth rather than stair-stepped.
        preferredRendererType: Shape.GeometryRenderer
        antialiasing: root.kind === "close"
        layer.enabled: root.kind === "close"
        layer.samples: 4

        ShapePath {
            objectName: "captionGlyphPath"
            fillColor: "transparent"
            strokeColor: root.color
            strokeWidth: root.strokeLogical
            capStyle: ShapePath.FlatCap
            joinStyle: ShapePath.MiterJoin

            PathMultiline {
                paths: root.polylines
            }
        }
    }
}

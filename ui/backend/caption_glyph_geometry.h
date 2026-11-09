#ifndef SUBREC_UI_CAPTION_GLYPH_GEOMETRY_H
#define SUBREC_UI_CAPTION_GLYPH_GEOMETRY_H

// Window caption glyphs (§3.1 title bar: minimize / maximize / restore / close) drawn as
// strokes on the device-pixel grid. Header-only and GL-free so tests include it directly.
// CaptionGlyph.qml repeats these formulas in JavaScript with the same operation order
// (ui_controls_tests compares the two); change both together.
//
// Model: the glyph item is iconLogical (16) logical px square and its origin is taken to
// sit on a device-pixel edge. Inside it a glyphLogical (10) box is laid out in logical
// coordinates and every coordinate is snapped:
//   - stroke width: physicalStrokePx(dpr) whole device pixels (1 / 1 / 2 / 2 px at
//     100 / 125 / 150 / 200 %), i.e. logicalStroke(dpr) logical px;
//   - a stroke's centre line: snapToPhysicalCenter, so both stroke edges are pixel edges;
//   - where a flat-capped stroke ends: snapToPhysicalEdge.
// Each stroke therefore covers whole device pixels and nothing else, so an aliased renderer
// draws it solid and an anti-aliased one has no partial fringe on the straight kinds.

#include <QPointF>
#include <QString>
#include <QtGlobal>
#include <cmath>
#include <vector>

namespace subrec::ui::caption {

// Rounds like JavaScript Math.round (halves go up, negatives too) so C++ and QML agree.
inline qreal jsRound(qreal value)
{
    return std::floor(value + 0.5);
}

// Stroke width in device pixels: the nearest whole number, at least 1.
inline int physicalStrokePx(qreal dpr)
{
    return qMax(1, int(jsRound(dpr)));
}

// The same width in logical px (what ShapePath.strokeWidth takes).
inline qreal logicalStroke(qreal dpr)
{
    return physicalStrokePx(dpr) / dpr;
}

// Nearest device-pixel edge to `logical`, in logical px.
inline qreal snapToPhysicalEdge(qreal logical, qreal dpr)
{
    return jsRound(logical * dpr) / dpr;
}

// Centre line (logical px) of a physicalStrokePx(dpr)-wide stroke placed nearest to
// `logical`: centre +- half the width are both device-pixel edges.
inline qreal snapToPhysicalCenter(qreal logical, qreal dpr)
{
    const qreal half = physicalStrokePx(dpr) / 2.0;
    return (jsRound(logical * dpr - half) + half) / dpr;
}

using Polyline = std::vector<QPointF>;

// Stroke polylines (logical px, item origin at 0,0) for `kind`: "minimize", "maximize",
// "restore" or "close"; anything else -> none. Drawn with flat caps and miter joins.
//   minimize: one horizontal line across the box, vertically centred.
//   maximize: the box outline, one closed polyline starting and ending mid-top (so all four
//             corners are joins, none is a pair of flat caps).
//   restore:  front box (lower left, closed like maximize) and the back box's visible top and
//             right edges, offset by about 2 logical px (at least one free device pixel).
//   close:    the two diagonals, ends inset by half a stroke.
inline std::vector<Polyline> glyphPolylines(const QString &kind, qreal dpr,
                                            qreal glyphLogical = 10, qreal iconLogical = 16)
{
    const qreal half = logicalStroke(dpr) / 2;
    const qreal a = snapToPhysicalEdge((iconLogical - glyphLogical) / 2, dpr);   // box left/top edge
    const qreal b = snapToPhysicalEdge((iconLogical + glyphLogical) / 2, dpr);   // box right/bottom edge
    const qreal lo = a + half;   // centre of a stroke lying just inside the box
    const qreal hi = b - half;
    if (kind == QLatin1String("minimize")) {
        const qreal y = snapToPhysicalCenter(iconLogical / 2, dpr);
        return {{QPointF(a, y), QPointF(b, y)}};
    }
    if (kind == QLatin1String("maximize")) {
        const qreal mid = snapToPhysicalEdge(iconLogical / 2, dpr);
        return {{QPointF(mid, lo), QPointF(hi, lo), QPointF(hi, hi), QPointF(lo, hi),
                 QPointF(lo, lo), QPointF(mid, lo)}};
    }
    if (kind == QLatin1String("restore")) {
        const qreal d = qMax(snapToPhysicalEdge(2, dpr), (physicalStrokePx(dpr) + 1) / dpr);
        const qreal frontRight = b - d - half;
        const qreal frontTop = a + d + half;
        const qreal mid = snapToPhysicalEdge((a + b - d) / 2, dpr);
        return {{QPointF(mid, frontTop), QPointF(frontRight, frontTop), QPointF(frontRight, hi),
                 QPointF(lo, hi), QPointF(lo, frontTop), QPointF(mid, frontTop)},
                // Back box: from the front box's top edge up, across, down, and back to the
                // front box's right edge.
                {QPointF(a + d + half, a + d), QPointF(a + d + half, lo), QPointF(hi, lo),
                 QPointF(hi, b - d - half), QPointF(b - d, b - d - half)}};
    }
    if (kind == QLatin1String("close"))
        return {{QPointF(lo, lo), QPointF(hi, hi)}, {QPointF(hi, lo), QPointF(lo, hi)}};
    return {};
}

} // namespace subrec::ui::caption

#endif // SUBREC_UI_CAPTION_GLYPH_GEOMETRY_H

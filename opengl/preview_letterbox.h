#ifndef SUBREC_PREVIEW_LETTERBOX_H
#define SUBREC_PREVIEW_LETTERBOX_H

// Preview letterbox geometry (§3.2.2 B keepRatio). Header-only and GL-free so tests can use
// it without an OpenGL context.

#include <QRect>
#include <QSize>
#include <cmath>

namespace preview {

// Viewport inside a target of size `target` for a frame of size `frame`.
// keepRatio false, or no valid frame yet: the whole target (stretch, as before).
// keepRatio true: the largest rectangle with the frame's aspect ratio, centred; bars on
// the two sides that do not fit. Symmetric, so the same rectangle works with GL's
// bottom-left origin.
inline QRect letterboxRect(const QSize &target, const QSize &frame, bool keepRatio)
{
    const QRect full(QPoint(0, 0), target);
    if (!keepRatio || target.width() <= 0 || target.height() <= 0
        || frame.width() <= 0 || frame.height() <= 0)
        return full;
    const double frameAspect = double(frame.width()) / double(frame.height());
    const double targetAspect = double(target.width()) / double(target.height());
    int w = target.width();
    int h = target.height();
    if (frameAspect > targetAspect)
        h = qMax(1, int(std::lround(w / frameAspect)));   // wider source: bars top/bottom
    else
        w = qMax(1, int(std::lround(h * frameAspect)));   // taller source: bars left/right
    return QRect((target.width() - w) / 2, (target.height() - h) / 2, w, h);
}

} // namespace preview

#endif // SUBREC_PREVIEW_LETTERBOX_H

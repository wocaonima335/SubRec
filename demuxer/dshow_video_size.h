#ifndef DSHOW_VIDEO_SIZE_H
#define DSHOW_VIDEO_SIZE_H

#include <cmath>

struct DshowVideoSize
{
    int width = 0;
    int height = 0;
};

// QScreen::size() is in device-independent pixels; the DShow screen capture filter needs
// the screen's physical pixels (2560x1440 at 150 % is reported as 1707x960). Scale by the
// device pixel ratio, round to the nearest pixel (Qt rounds the logical size, so 1707 * 1.5
// gives 2560.5), then down to even sizes for yuv420p. Returns {0, 0} for invalid input.
inline DshowVideoSize dshowPhysicalVideoSize(int logicalWidth, int logicalHeight, double devicePixelRatio)
{
    if (logicalWidth <= 0 || logicalHeight <= 0 || !(devicePixelRatio > 0.0))
        return {};
    const auto physical = [devicePixelRatio](int logical) {
        const long rounded = std::lround(logical * devicePixelRatio);
        return rounded >= 2 ? static_cast<int>(rounded) & ~1 : 2;
    };
    return {physical(logicalWidth), physical(logicalHeight)};
}

#endif // DSHOW_VIDEO_SIZE_H

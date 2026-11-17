#include "demuxer/dshow_video_size.h"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
void expectSize(int w, int h, double dpr, int expectedW, int expectedH)
{
    const DshowVideoSize got = dshowPhysicalVideoSize(w, h, dpr);
    if (got.width != expectedW || got.height != expectedH)
        throw std::runtime_error(std::to_string(w) + "x" + std::to_string(h) + " @" + std::to_string(dpr) + " -> "
                                 + std::to_string(got.width) + "x" + std::to_string(got.height) + ", expected "
                                 + std::to_string(expectedW) + "x" + std::to_string(expectedH));
}
} // namespace

int main()
{
    try {
        expectSize(1920, 1080, 1.0, 1920, 1080);
        expectSize(1366, 768, 1.0, 1366, 768);
        expectSize(1536, 864, 1.25, 1920, 1080);   // 1920x1080 at 125 %
        expectSize(1280, 720, 1.5, 1920, 1080);    // 1920x1080 at 150 %
        expectSize(1707, 960, 1.5, 2560, 1440);    // 2560x1440 at 150 % (Qt rounds 1706.67 up)
        expectSize(2048, 1152, 1.25, 2560, 1440);  // 2560x1440 at 125 %
        expectSize(1920, 1080, 2.0, 3840, 2160);   // 4K at 200 %
        expectSize(2560, 1440, 1.5, 3840, 2160);   // 4K at 150 %
        expectSize(1463, 823, 1.75, 2560, 1440);   // 2560x1440 at 175 % (2560.25 x 1440.25)
        expectSize(1824, 1216, 1.5, 2736, 1824);   // 3:2 2736x1824 at 150 %
        expectSize(1825, 1216, 1.5, 2738, 1824);   // 2737.5 -> 2738 (nearest, already even)
        expectSize(1365, 767, 1.0, 1364, 766);     // odd logical sizes become even
        expectSize(0, 1080, 1.0, 0, 0);
        expectSize(1920, 1080, 0.0, 0, 0);
        expectSize(1, 1, 1.0, 2, 2);
        std::puts("PASS DShow video_size: logical size x DPR, nearest pixel, even");
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL %s\n", e.what());
        return 1;
    }
    return 0;
}

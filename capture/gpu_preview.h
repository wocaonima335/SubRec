#ifndef GPU_PREVIEW_H
#define GPU_PREVIEW_H
#include <functional>
#include <memory>
#include <string>
struct AVFrame;
// Software half of the split branch. It has its own graph/thread; never run hwdownload
// on the recording graph's thread. Consumer returns false when a GUI frame was dropped.
inline std::string gpuPreviewFilterSpec(bool hardware = true)
{
    // Rate limit before GPU transfer. No upscaling, even dimensions including portrait.
    return std::string("fps=8,") + (hardware ? "hwdownload,format=bgra," : "")
        + "scale=w=-2:h='max(2,trunc(min(ih,360)/2)*2)':flags=bilinear:"
          "out_color_matrix=bt601:out_range=tv,format=yuv420p";
}
class GpuPreview
{
public:
    using Consumer = std::function<bool(const AVFrame *)>;
    explicit GpuPreview(Consumer consumer);
    ~GpuPreview();
    void offer(const AVFrame *frame);
private:
    struct State;
    std::shared_ptr<State> state_;
    class Lifetime;
    std::unique_ptr<Lifetime> lifetime_;
};
#endif

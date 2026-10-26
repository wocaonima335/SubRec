#ifndef DDAGRAB_SCREEN_SOURCE_H
#define DDAGRAB_SCREEN_SOURCE_H

#include "capture_types.h"

#include <QString>
#include <QStringList>

struct AVFilterGraph;
struct AVFilterContext;
struct AVFrame;
struct AVBufferRef;

// How ddagrab frames leave the filter graph.
enum class DdagrabFrameMode
{
    Auto, // Gpu if an encoder accepts the D3D11 frames (probe), otherwise Cpu
    Gpu,  // AV_PIX_FMT_D3D11 frames (hw_frames_ctx) straight to h264_nvenc / h264_amf
    Cpu   // ddagrab -> hwdownload -> bgra -> yuv420p system-memory frames (original chain)
};

struct DdagrabOptions
{
    int outputIndex = 0;   // DXGI output of the default adapter (ddagrab "output_idx")
    int frameRate = 30;    // must match FFVFilter::getFrameRate()
    bool drawMouse = true;
    DdagrabFrameMode mode = DdagrabFrameMode::Auto;

    // outputIndex from InputRequest::url ("output_idx=N"); mode from SUBREC_DDAGRAB_MODE
    // (auto|gpu|cpu), output from SUBREC_DDAGRAB_OUTPUT if set (overrides the request).
    static DdagrabOptions fromRequest(const InputRequest &request);
};

// DXGI Desktop Duplication screen source built on FFmpeg's ddagrab filter. It is a frame
// source (no demuxer, no decoder): read() returns frames ready for FFVFilterThread.
// Not thread-safe: open/read/close must run on one thread (FFScreenGrabThread does that, the
// DXGI duplication is then created and used on the same thread).
class DdagrabScreenSource
{
public:
    DdagrabScreenSource();
    ~DdagrabScreenSource();
    DdagrabScreenSource(const DdagrabScreenSource &) = delete;
    DdagrabScreenSource &operator=(const DdagrabScreenSource &) = delete;

    // True when the linked FFmpeg contains the ddagrab filter.
    static bool isAvailable();
    // Encoders (by name) that accept AV_PIX_FMT_D3D11 frames via hw_frames_ctx, from the
    // codecs' own hw_configs; currently h264_nvenc and h264_amf. Order = probe order.
    static QStringList d3d11FrameEncoders();

    // forceCpuFrames: reopen after a lost duplication must keep the frame format the encoder
    // was opened with (system memory), whatever SUBREC_DDAGRAB_MODE says.
    CaptureStartResult open(const InputRequest &request, bool forceCpuFrames = false);
    // 0: frame filled; AVERROR(EAGAIN): nothing yet; AVERROR_OUTPUT_CHANGED / other < 0: the
    // duplication is gone (desktop switch, UAC, resolution change): close() and reopen.
    int read(AVFrame *frame);
    void close();

    bool isOpen() const { return m_sink != nullptr; }
    bool gpuFrames() const { return m_gpuFrames; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    // Encoder that passed the GPU-frame probe (empty for Cpu mode or forced Gpu mode).
    QString probedEncoder() const { return m_probedEncoder; }
    QString describe() const;

private:
    int buildGraph(bool gpuFrames, QString *error);
    void freeGraph();
    bool probeGpuEncoder(AVBufferRef *framesContext);

    DdagrabOptions m_options;
    AVFilterGraph *m_graph = nullptr;
    AVFilterContext *m_sink = nullptr;
    bool m_gpuFrames = false;
    int m_width = 0;
    int m_height = 0;
    QString m_probedEncoder;
};

#endif // DDAGRAB_SCREEN_SOURCE_H

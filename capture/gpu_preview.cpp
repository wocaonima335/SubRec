#include "gpu_preview.h"
#include "latest_preview_slot.h"
#include "wasapi_worker_lifetime.h"
#include <QDebug>
#include <algorithm>
#include <cstdint>
#include <exception>
#include <atomic>
#include <chrono>
#include <thread>
extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/hwcontext.h>
#include <libavutil/time.h>
#include <libavutil/error.h>
}

namespace {
QString ffmpegErrorText(int errorCode)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror(errorCode, buffer, sizeof(buffer)) < 0) {
        return QStringLiteral("unknown FFmpeg error");
    }
    return QString::fromLocal8Bit(buffer);
}
} // namespace

struct GpuPreview::State {
    struct Release { void operator()(AVFrame *p) const { av_frame_free(&p); } };
    using Frame = std::unique_ptr<AVFrame, Release>;
    LatestPreviewSlot<Frame> pending;
    Consumer consumer;
    std::mutex consumerMutex;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> dropped{0};
    AVFilterGraph *graph = nullptr;
    AVFilterContext *input = nullptr, *output = nullptr;
    ~State() { avfilter_graph_free(&graph); }

    int configure(const AVFrame *frame) {
        graph = avfilter_graph_alloc();
        if (!graph) return AVERROR(ENOMEM);
        graph->nb_threads = 1;
        input = avfilter_graph_alloc_filter(graph, avfilter_get_by_name("buffer"), "in");
        if (!input) return AVERROR(ENOMEM);
        AVBufferSrcParameters *params = av_buffersrc_parameters_alloc();
        if (!params) return AVERROR(ENOMEM);
        params->format = frame->format;
        params->width = frame->width;
        params->height = frame->height;
        params->time_base = AVRational{1, 1000000};
        params->sample_aspect_ratio = AVRational{1, 1};
        params->hw_frames_ctx = frame->hw_frames_ctx;
        int ret = av_buffersrc_parameters_set(input, params); // refs the hardware context
        av_free(params);
        if (ret < 0) return ret;
        ret = avfilter_init_str(input, nullptr);
        if (ret < 0) return ret;
        ret = avfilter_graph_create_filter(&output, avfilter_get_by_name("buffersink"), "out",
                                           nullptr, nullptr, graph);
        if (ret < 0) return ret;
        AVFilterInOut *in = avfilter_inout_alloc(), *out = avfilter_inout_alloc();
        if (!in || !out) {
            avfilter_inout_free(&in); avfilter_inout_free(&out);
            return AVERROR(ENOMEM);
        }
        in->name = av_strdup("out"); in->filter_ctx = output;
        out->name = av_strdup("in"); out->filter_ctx = input;
        const std::string spec = gpuPreviewFilterSpec();
        ret = avfilter_graph_parse_ptr(graph, spec.c_str(), &in, &out, nullptr);
        avfilter_inout_free(&in); avfilter_inout_free(&out);
        if (ret < 0) return ret;
        return avfilter_graph_config(graph, nullptr);
    }
    void run() {
        Frame latest(av_frame_alloc()), next(av_frame_alloc());
        if (!latest || !next) {
            qWarning() << "[Preview] unable to allocate conversion frames; recording continues";
            stop = true; return;
        }
        uint64_t frames = 0, previousFrames = 0;
        int64_t logStart = av_gettime_relative(), lastLog = logStart;
        bool configured = false;
        while (!stop.load(std::memory_order_acquire)) {
            auto frame = pending.take();
            if (!frame) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
            if (!configured) {
                const int ret = configure(frame.get());
                if (ret < 0) {
                    qWarning().noquote() << QStringLiteral("[Preview] GPU preview conversion unavailable; recording continues: %1 (%2)")
                        .arg(ffmpegErrorText(ret)).arg(ret);
                    stop = true; break;
                }
                configured = true;
            }
            // Monotonic capture time, independent of encoder PTS rewriting on the main branch.
            frame->pts = av_gettime_relative() - logStart;
            int ret = av_buffersrc_add_frame_flags(input, frame.get(), 0);
            if (ret < 0) { ++dropped; continue; }
            // This graph has a buffer source, so EAGAIN never waits for a native capture.
            // Drain every turn even with no UI consumer, retaining only the latest result.
            av_frame_unref(latest.get());
            while (!stop.load(std::memory_order_acquire)
                   && (ret = av_buffersink_get_frame(output, next.get())) >= 0) {
                if (latest->buf[0]) ++dropped;
                av_frame_unref(latest.get());
                av_frame_move_ref(latest.get(), next.get());
            }
            if (stop.load(std::memory_order_acquire)) break;
            if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                qWarning() << "[Preview] GPU preview conversion failed" << ret;
                stop = true; break;
            }
            if (latest->buf[0] && !stop.load(std::memory_order_acquire)) {
                ++frames;
                std::lock_guard<std::mutex> lock(consumerMutex);
                if (!stop.load() && (!consumer || !consumer(latest.get()))) ++dropped;
                av_frame_unref(latest.get());
            }
            const int64_t now = av_gettime_relative();
            if (now - lastLog >= 5000000) {
                qInfo().noquote() << QStringLiteral("[Preview] gpu-branch frames=%1 fps=%2 dropped=%3")
                    .arg(frames).arg((frames - previousFrames) * 1000000.0 / (now - lastLog), 0, 'f', 1)
                    .arg(dropped.load());
                previousFrames = frames; lastLog = now;
            }
        }
        qInfo().noquote() << QStringLiteral("[Preview] gpu-branch frames=%1 fps=%2 dropped=%3 (stopped)")
            .arg(frames).arg(frames * 1000000.0 / std::max<int64_t>(1, av_gettime_relative()-logStart), 0, 'f', 1)
            .arg(dropped.load());
    }
};
class GpuPreview::Lifetime : public capture::wasapi::WorkerLifetime {};
GpuPreview::GpuPreview(Consumer consumer) : state_(std::make_shared<State>()), lifetime_(new Lifetime)
{
    state_->consumer = std::move(consumer);
    lifetime_->start([state = state_] {
        try {
            state->run();
        } catch (const std::exception &error) {
            state->stop.store(true, std::memory_order_release);
            qWarning() << "[Preview] worker failed; recording continues:" << error.what();
        } catch (...) {
            state->stop.store(true, std::memory_order_release);
            qWarning() << "[Preview] worker failed; recording continues";
        }
    });
}
GpuPreview::~GpuPreview()
{
    state_->stop.store(true, std::memory_order_release);
    {
        // Revoke callbacks before allowing the owner/UI to be destroyed. GPU work never
        // holds this mutex, and PreviewBridge submission only tries its own lock.
        std::lock_guard<std::mutex> lock(state_->consumerMutex);
        state_->consumer = {};
    }
    // A stuck GPU transfer owns only its state and hardware references, never the capture
    // graph or recorder. Abandoning it cannot delay recording shutdown indefinitely.
    if (!lifetime_->joinFor(std::chrono::milliseconds(500))) lifetime_->abandon();
}
void GpuPreview::offer(const AVFrame *frame)
{
    if (state_->stop.load(std::memory_order_acquire)) return;
    State::Frame copy(av_frame_clone(frame));
    if (!copy || !state_->pending.offer(std::move(copy))) ++state_->dropped;
}

#pragma once
#include "wasapi_audio_sink.h"
#include "wasapi_worker_lifetime.h"
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <vector>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libavutil/dict.h>
}

namespace capture::wasapi {
// push/finish are serialized on the capture producer; cancel/join are control-thread
// operations. One delivery consumer owns the downstream sink. Only that thread may
// call the downstream sink. Preallocated AVFrame shells and acquire/release
// indices keep push/finish free of queue locks, waits, allocations and logging.
// Ten seconds of 48 kHz stereo float PCM is ~3.7 MiB; a separate slot bound also
// limits tiny frames. On either limit, drop newest and retain original PTS (and
// the first AAC input/priming timeline). Delivery replaces dropped intervals with
// silence, including an overflow tail at EOF. Never make the capture device wait.
class BufferedAudioSink final : public AudioFrameSink {
    static_assert(std::atomic<uint64_t>::is_always_lock_free, "SPSC indices must be lock free");
    struct FrameDelete { void operator()(AVFrame *f) const { av_frame_free(&f); } };
    using Frame = std::unique_ptr<AVFrame, FrameDelete>;
    struct Gap {
        int64_t pts = AV_NOPTS_VALUE, captureUs = AV_NOPTS_VALUE;
        uint64_t samples = 0;
    };
    struct Slot { Frame frame; Gap gap; };
    struct State {
        std::shared_ptr<AudioFrameSink> downstream;
        std::function<void()> onFirstFrame;
        std::vector<Slot> ring_;
        Gap pendingGap; // producer-only until EOF publication
        const uint64_t maxSamples;
        alignas(64) std::atomic<uint64_t> written{0};
        alignas(64) std::atomic<uint64_t> read{0};
        std::atomic<uint64_t> acceptedSamples{0}, retiredSamples{0};
        std::atomic<uint64_t> silenceSamples{0};
        std::atomic<uint64_t> overflowSamples{0}, overflowFrames{0}, rejectedSamples{0};
        std::atomic<uint64_t> rejectedFrames{0}, acceptedFrames{0}, retiredFrames{0};
        std::atomic<bool> eof{false}, cancelled{false}, failed{false};
        State(std::shared_ptr<AudioFrameSink> sink, size_t capacity, uint64_t limit, std::function<void()> firstFrame)
            : downstream(std::move(sink)), onFirstFrame(std::move(firstFrame)), maxSamples(limit) {
            if (!downstream || !capacity || !limit) throw std::invalid_argument("audio buffer capacity");
            ring_.reserve(capacity);
            for (size_t i = 0; i < capacity; ++i) {
                Frame f(av_frame_alloc());
                if (!f) throw std::bad_alloc();
                ring_.push_back({std::move(f), {}});
            }
        }
        bool deliverGap(const Gap &gap) {
            // Compact gap records bound memory even under an indefinitely stalled
            // consumer. Reconstruct lost PCM as silence HERE, never on capture.
            // This preserves duration even in sample-count-based encoder paths.
            uint64_t offset = 0;
            while (offset < gap.samples) {
                if (cancelled.load()) return false;
                Frame f(av_frame_alloc());
                if (!f) throw std::bad_alloc();
                f->format = AV_SAMPLE_FMT_FLTP; f->sample_rate = kOutputSampleRate;
                av_channel_layout_default(&f->ch_layout, kOutputChannels);
                f->nb_samples = static_cast<int>(std::min<uint64_t>(kOutputFrameSamples, gap.samples-offset));
                f->pts = gap.pts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE : gap.pts + offset;
                f->time_base = {1,kOutputSampleRate}; f->duration = f->nb_samples;
                if (av_frame_get_buffer(f.get(), 0) < 0) throw std::bad_alloc();
                av_samples_set_silence(f->extended_data, 0, f->nb_samples, kOutputChannels, AV_SAMPLE_FMT_FLTP);
                if (gap.captureUs != AV_NOPTS_VALUE &&
                    av_dict_set_int(&f->metadata, "capture_time_us",
                        gap.captureUs + offset*1000000/kOutputSampleRate, 0) < 0) throw std::bad_alloc();
                const int samples = f->nb_samples;
                if (!downstream->push(f.get())) return false;
                silenceSamples += samples; offset += samples;
            }
            return true;
        }
        void deliver() {
            try {
                Frame frame(av_frame_alloc());
                if (!frame) throw std::bad_alloc();
                for (;;) {
                    if (cancelled.load(std::memory_order_acquire)) return;
                    const auto r = read.load(std::memory_order_relaxed);
                    // Load EOF first: observing it also observes the last publication.
                    const bool finished = eof.load(std::memory_order_acquire);
                    if (r == written.load(std::memory_order_acquire)) {
                        if (finished) {
                            if (deliverGap(pendingGap)) downstream->finish();
                            else failed = true;
                            return;
                        }
                        // Only the delivery worker sleeps; producer need not signal a CV.
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        continue;
                    }
                    if (onFirstFrame) {
                        auto notify = std::move(onFirstFrame);
                        onFirstFrame = {};
                        notify(); // e.g. startup logging, never on capture
                    }
                    auto &slot = ring_[r % ring_.size()];
                    const auto gap = slot.gap;
                    av_frame_move_ref(frame.get(), slot.frame.get());
                    const auto samples = frame->nb_samples;
                    read.store(r + 1, std::memory_order_release);
                    // Keep the in-flight frame charged to the sample budget while
                    // blocked downstream. Cancellation wakes QueueAudioSink's wait.
                    if (!deliverGap(gap)) { failed = true; return; }
                    const bool ok = downstream->push(frame.get());
                    av_frame_unref(frame.get());
                    if (!ok) { rejectedSamples += samples; ++rejectedFrames; }
                    retiredSamples += samples; ++retiredFrames;
                    if (!ok) { failed = true; return; }
                }
            } catch (...) {
                failed = true;
                downstream->cancel();
                throw;
            }
        }
    };
public:
    static constexpr uint64_t kCapacitySamples = 10 * kOutputSampleRate;
    static constexpr size_t kCapacityFrames = 2048;
    explicit BufferedAudioSink(std::shared_ptr<AudioFrameSink> sink,
                               size_t capacity = kCapacityFrames,
                               uint64_t samples = kCapacitySamples, std::function<void()> firstFrame = {})
        : state_(std::make_shared<State>(std::move(sink), capacity, samples, std::move(firstFrame))) {
        worker_.start([state = state_] { state->deliver(); });
    }
    ~BufferedAudioSink() override {
        cancel();
        // No borrowed owner/queue storage: an abandoned delivery task owns State.
        if (!worker_.joinFor(std::chrono::milliseconds(0))) worker_.abandon();
    }
    bool push(AVFrame *frame) override {
        auto &s = *state_;
        const auto samples = static_cast<uint64_t>(frame->nb_samples);
        if (s.cancelled.load() || s.failed.load() || s.eof.load()) {
            s.rejectedSamples += samples; ++s.rejectedFrames;
            return false; // caller releases the unconsumed frame
        }
        const auto w = s.written.load(std::memory_order_relaxed);
        const auto pending = s.acceptedSamples.load(std::memory_order_relaxed) - s.retiredSamples.load();
        if (w - s.read.load(std::memory_order_acquire) == s.ring_.size() ||
            samples > s.maxSamples || pending > s.maxSamples - samples) {
            s.overflowSamples += samples; ++s.overflowFrames;
            if (!s.pendingGap.samples) {
                s.pendingGap.pts = frame->pts;
                const auto *time = av_dict_get(frame->metadata, "capture_time_us", nullptr, 0);
                s.pendingGap.captureUs = time ? std::strtoll(time->value, nullptr, 10) : AV_NOPTS_VALUE;
            }
            s.pendingGap.samples += samples;
            // Intentional downstream drop: source still advances its timeline.
            // Leave ownership to caller, avoiding release/destruction in this handoff.
            return true;
        }
        auto &slot = s.ring_[w % s.ring_.size()];
        slot.gap = s.pendingGap; s.pendingGap = {};
        av_frame_move_ref(slot.frame.get(), frame);
        s.acceptedSamples += samples; ++s.acceptedFrames;
        s.written.store(w + 1, std::memory_order_release);
        return true;
    }
    void finish() override { state_->eof.store(true, std::memory_order_release); }
    // Control-thread operation, never used by push/finish. Closing the downstream
    // queue wakes the delivery worker, including a full-queue wait. Do this once
    // so a late destructor cannot close a newer recorder session's queue.
    void cancel() noexcept override {
        if (!state_->cancelled.exchange(true)) state_->downstream->cancel();
    }
    bool joinFor(std::chrono::milliseconds timeout) override { return worker_.joinFor(timeout); }
    SinkDiagnostics diagnostics() const override {
        const auto &s = *state_;
        SinkDiagnostics result; // capture-side blockedNs/maxBlockedNs are always zero
        const auto downstream = s.downstream->diagnostics();
        result.deliveryBlockedNs = downstream.blockedNs;
        result.deliveryMaxBlockedNs = downstream.maxBlockedNs;
        result.downstreamDroppedSamples = s.overflowSamples.load() + s.rejectedSamples.load();
        result.downstreamDroppedFrames = s.overflowFrames.load() + s.rejectedFrames.load();
        result.rejectedSamples = s.rejectedSamples.load();
        result.downstreamSilenceSamples = s.silenceSamples.load();
        if (s.cancelled.load() || s.failed.load()) {
            // Includes ring/in-flight frames abandoned at abort; delivered frames
            // are retired. Final snapshot is taken after both threads have joined.
            const auto retired = s.retiredSamples.load();
            const auto accepted = s.acceptedSamples.load();
            result.downstreamDroppedSamples += accepted - retired;
            const auto retiredFrames = s.retiredFrames.load();
            result.downstreamDroppedFrames += s.acceptedFrames.load() - retiredFrames;
        }
        return result;
    }
private:
    std::shared_ptr<State> state_;
    WorkerLifetime worker_;
};
} // namespace capture::wasapi

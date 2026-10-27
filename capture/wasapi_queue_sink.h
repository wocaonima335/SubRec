#pragma once
#include "wasapi_audio_sink.h"
#include "queue/ffaframequeue.h"
#include <cstdlib>

namespace capture::wasapi {
class QueueAudioSink final : public AudioFrameSink {
public:
    explicit QueueAudioSink(FFAFrameQueue &queue, bool timestamped)
        : queue_(queue.sharedState()), timestamped_(timestamped) {}
    bool push(AVFrame *frame) override {
        const auto samples = frame->nb_samples;
        if (cancelled_) { rejectedSamples_ += samples; av_frame_unref(frame); return false; }
        if (timestamped_) {
            auto *entry = av_dict_get(frame->metadata, "capture_time_us", nullptr, 0);
            if (!entry) { rejectedSamples_ += samples; return false; }
            frame->pts = std::strtoll(entry->value, nullptr, 10);
            frame->time_base = {1,1000000};
        }
        uint64_t blockedNs = 0;
        const auto result = queue_->enqueueFromSrc(frame, &cancelled_, &blockedNs);
        blockedNs_ += blockedNs;
        auto maximum = maxBlockedNs_.load();
        while (maximum < blockedNs && !maxBlockedNs_.compare_exchange_weak(maximum, blockedNs)) {}
        if (result != MediaQueueResult::Ok) rejectedSamples_ += samples;
        return result == MediaQueueResult::Ok;
    }
    void finish() override { queue_->sendFinish(&cancelled_); }
    void cancel() noexcept override {
        // Close once: a late destructor/abort must not close a newer recording.
        if (!cancelled_.exchange(true)) queue_->close();
    }
    SinkDiagnostics diagnostics() const override {
        return {blockedNs_.load(), maxBlockedNs_.load(), rejectedSamples_.load()};
    }
private:
    std::atomic<uint64_t> blockedNs_{0}, maxBlockedNs_{0}, rejectedSamples_{0};
    std::shared_ptr<FFAFrameQueue::State> queue_;
    std::atomic<bool> cancelled_{false};
    bool timestamped_;
};
} // namespace capture::wasapi

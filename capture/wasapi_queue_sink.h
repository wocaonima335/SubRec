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
        if (cancelled_) { av_frame_unref(frame); return false; }
        if (timestamped_) {
            auto *entry = av_dict_get(frame->metadata, "capture_time_us", nullptr, 0);
            if (!entry) return false;
            frame->pts = std::strtoll(entry->value, nullptr, 10);
            frame->time_base = {1,1000000};
        }
        return queue_->enqueueFromSrc(frame, &cancelled_) == MediaQueueResult::Ok;
    }
    void finish() override { queue_->sendFinish(&cancelled_); }
    void cancel() noexcept override {
        // Close once: a late destructor/abort must not close a newer recording.
        if (!cancelled_.exchange(true)) queue_->close();
    }
private:
    std::shared_ptr<FFAFrameQueue::State> queue_;
    std::atomic<bool> cancelled_{false};
    bool timestamped_;
};
} // namespace capture::wasapi

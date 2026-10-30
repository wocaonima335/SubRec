#pragma once
#include "recorder/ffrecorder.h"
// Replace device selection/open only; CaptureSession, screen worker, deadlines,
// ownership and queues are production code. Never open a real device in CTest.
class FFOpenSourceEvent {
    FFRecorder *recorder_;
    InputRequest video_;
public:
    FFOpenSourceEvent(FFRecorder *recorder, const InputRequest &video, const InputRequest &)
        : recorder_(recorder), video_(video) {}
    void setAudioEnabled(bool) {}
    void setMixMicrophone(const std::optional<InputRequest> &) {}
    void setAudioFallback(const std::optional<InputRequest> &) {}
    void setVideoFallback(const std::optional<InputRequest> &) {}
    CaptureStartResult openAndStartResult(const std::function<bool()> &guard) {
        for (auto &worker : recorder_->audio) {
            worker->queue.start();
            worker->start();
        }
        recorder_->videoQueue.start();
        return recorder_->screen.prepareOpenAndWait(video_, &recorder_->videoQueue,
                                                    nullptr, 80, guard);
    }
};

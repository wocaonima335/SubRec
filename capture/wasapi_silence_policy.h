#pragma once
#include "wasapi_audio_sink.h"
#include <algorithm>
#include <cstdint>

namespace capture::wasapi {
// No Windows/FFmpeg dependency. Observation times and the session epoch must
// share the monotonic capture clock (av_gettime_relative in production).
class SilencePolicy {
public:
    static constexpr int64_t kIdleUs = 100000;
    static constexpr int64_t kSafetyUs = kIdleUs;
    static constexpr int64_t kMinimumGapSamples = kIdleUs * kOutputSampleRate / 1000000;
    static constexpr unsigned kMinimumEmptyPolls = 3;

    explicit SilencePolicy(int64_t startUs) : startUs_(startUs) {}
    // SILENT and timestamp-error packets are still packets, not idle evidence.
    void packet() { resetIdle(); }
    // Device loss/reopen resets evidence but preserves the session epoch.
    void reset() { resetIdle(); }

    // Call once per completed poll after draining WASAPI. Seeing any packet,
    // unknown buffer state, clock reversal, or device loss breaks the streak.
    // Pending converter samples are reserved, so padding cannot duplicate its
    // delayed tail when the next real packet arrives or the converter drains.
    int64_t target(int64_t nowUs, int64_t outputSamples, int64_t pendingSamples,
                   bool receivedPacket, bool bufferEmpty) {
        if (receivedPacket || !bufferEmpty) {
            resetIdle();
            return outputSamples;
        }
        if (emptyPolls_ == 0 || nowUs < lastPollUs_) {
            emptySinceUs_ = nowUs;
            emptyPolls_ = 0;
        }
        lastPollUs_ = nowUs;
        if (emptyPolls_ < kMinimumEmptyPolls) ++emptyPolls_;
        if (emptyPolls_ < kMinimumEmptyPolls || nowUs - emptySinceUs_ < kIdleUs)
            return outputSamples;
        if (nowUs <= startUs_ || nowUs - startUs_ <= kSafetyUs) return outputSamples;
        const int64_t elapsedUs = nowUs - startUs_ - kSafetyUs;
        // Split seconds/remainder to avoid overflowing on a long-running clock.
        const int64_t end = (elapsedUs / 1000000) * kOutputSampleRate +
                            (elapsedUs % 1000000) * kOutputSampleRate / 1000000 -
                            std::max<int64_t>(pendingSamples, 0);
        return std::max(outputSamples, end);
    }
private:
    void resetIdle() { emptyPolls_ = 0; }
    const int64_t startUs_;
    int64_t emptySinceUs_ = 0, lastPollUs_ = 0;
    unsigned emptyPolls_ = 0;
};
}

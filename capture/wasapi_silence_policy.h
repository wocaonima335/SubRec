#pragma once
#include "wasapi_audio_sink.h"
#include <algorithm>
#include <cstdint>

namespace capture::wasapi {
// No Windows, FFmpeg or wall-clock dependency. The caller supplies observation
// times only to qualify a continuous idle interval, never as a fill destination.
class SilencePolicy {
public:
    static constexpr int64_t kIdleUs = 100000;
    static constexpr int64_t kMinimumGapSamples = kOutputSampleRate / 10;
    static constexpr unsigned kMinimumEmptyPolls = 3;

    // endSamples is the last packet's QPC start + duration on the session's
    // output timeline. A timestamp-error packet must pass -1, not an estimate
    // based on its arrival time. SILENT packets are still real packets: their
    // zero samples go through AudioPipeline normally and reset the idle streak.
    void packet(int64_t endSamples) {
        lastPacketEnd_ = endSamples;
        resetIdle();
    }
    void reset() { lastPacketEnd_ = -1; resetIdle(); }

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
        if (lastPacketEnd_ <= outputSamples || lastPacketEnd_ - outputSamples < kMinimumGapSamples)
            return outputSamples;
        const int64_t end = lastPacketEnd_ - std::max<int64_t>(pendingSamples, 0);
        return std::max(outputSamples, end);
    }
private:
    void resetIdle() { emptyPolls_ = 0; }
    int64_t lastPacketEnd_ = -1, emptySinceUs_ = 0, lastPollUs_ = 0;
    unsigned emptyPolls_ = 0;
};
}

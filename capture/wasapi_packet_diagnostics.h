#pragma once
#include "wasapi_audio_sink.h"
#include <cstdint>
#include <limits>

namespace capture::wasapi {
// Observation only: never retimes or drops audio. Device positions reset on
// reopen. A discontinuity flag alone does not specify how many samples were lost.
struct PacketDiagnostics {
    uint64_t discontinuities = 0, estimatedLostSamples = 0, unquantifiedDiscontinuities = 0;
    void resetPosition() { haveEnd_ = false; }
    void observe(uint64_t position, uint32_t frames, int rate, bool discontinuity, bool timestampValid) {
        const bool valid = timestampValid && rate > 0 && position <= std::numeric_limits<uint64_t>::max()-frames;
        if (discontinuity) {
            ++discontinuities;
            if (valid && haveEnd_ && rate == rate_ && position >= end_) {
                const auto gap = position-end_;
                // Convert device frames to the fixed output sample rate; exclude
                // unflagged idle gaps and gaps across different devices.
                estimatedLostSamples += gap / rate * kOutputSampleRate +
                    gap % rate * kOutputSampleRate / rate;
            } else ++unquantifiedDiscontinuities;
        }
        haveEnd_ = valid;
        if (valid) { end_ = position+frames; rate_ = rate; }
    }
private:
    bool haveEnd_ = false;
    uint64_t end_ = 0;
    int rate_ = 0;
};
}

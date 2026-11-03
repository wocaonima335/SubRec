#pragma once

#include <algorithm>
#include <cstdint>

extern "C" {
#include <libavutil/mathematics.h>
#include <libavutil/rational.h>
}

// Chooses the capture timestamp of a screen frame (P3a). Preferred source is the
// time the desktop image was presented (DXGI LastPresentTime exported by the
// SubRec ddagrab patch and mapped to the session clock); the consumer receive
// time is only a counted fallback. Pure logic, single owner (the grab thread).
//
// - New image with a plausible present time: that time.
// - Duplicate / pointer-only frame: previous new image + k frame durations
//   (k = duplicates since that image), never later than the receive time. The
//   same picture is on screen throughout, so any time in that range is honest;
//   this keeps duplicates on the tick grid instead of mixing receive times in.
// - Missing metadata or an implausible present time (in the future, or older
//   than maxAgeUs): receive time, counted as fallback.
// - Held frame (lock-screen hold, capture recovery; stampHeld): receive time. It
//   repeats an old picture, so it ends the duplicate chain. The first new image
//   after a hold whose presentation time is not after the last held stamp (the
//   desktop image was presented while the hold was running) gets its receive
//   time instead, counted as afterHold: presentation time never moves backwards
//   past frames that were already queued.
// - Output is strictly increasing (+1 us when needed, counted as clamped).
class PresentStamper
{
public:
    struct Input {
        bool hasPresent = false;   // ddagrab.present_qpc mapped successfully
        int64_t presentUs = 0;     // session clock
        bool isNew = false;        // ddagrab.present_new == "1"
        int64_t receiveUs = 0;     // av_gettime_relative() right after read()
    };
    struct Counters {
        uint64_t present = 0, synthesized = 0, fallback = 0, held = 0;
        uint64_t rejectedFuture = 0, rejectedStale = 0, clamped = 0, afterHold = 0;
        int64_t maxLatencyUs = 0;    // receive - present, new images only
        int64_t sumLatencyUs = 0;
    };

    explicit PresentStamper(AVRational fps = AVRational{30, 1}, int64_t maxAgeUs = 250000,
                            int64_t futureToleranceUs = 2000)
        : frameUs_(fps.num > 0 && fps.den > 0 ? av_rescale(1000000, fps.den, fps.num) : 33333),
          maxAgeUs_(maxAgeUs), futureToleranceUs_(futureToleranceUs) {}

    void reset()
    {
        counters_ = Counters{};
        lastStampUs_ = lastNewUs_ = kNone;
        duplicates_ = 0;
        afterHold_ = false;
    }

    // Re-queued copy of an older picture (lock hold / capture recovery).
    int64_t stampHeld(int64_t receiveUs)
    {
        ++counters_.held;
        lastNewUs_ = kNone;
        duplicates_ = 0;
        afterHold_ = true;
        return monotonic(receiveUs);
    }

    int64_t stamp(const Input &in)
    {
        int64_t candidate;
        const bool afterHold = afterHold_;
        afterHold_ = false;
        if (in.hasPresent && in.isNew && afterHold && lastStampUs_ != kNone && in.presentUs <= lastStampUs_) {
            ++counters_.afterHold;
            candidate = fallback(in);
        } else if (in.hasPresent && in.isNew) {
            if (in.presentUs > in.receiveUs + futureToleranceUs_) {
                ++counters_.rejectedFuture;
                candidate = fallback(in);
            } else if (in.receiveUs - in.presentUs > maxAgeUs_) {
                ++counters_.rejectedStale;
                candidate = fallback(in);
            } else {
                candidate = in.presentUs;
                const int64_t latency = in.receiveUs - in.presentUs;
                counters_.maxLatencyUs = std::max(counters_.maxLatencyUs, latency);
                counters_.sumLatencyUs += latency;
                ++counters_.present;
                lastNewUs_ = in.presentUs;
                duplicates_ = 0;
            }
        } else if (in.hasPresent && lastNewUs_ != kNone) {
            ++duplicates_;
            candidate = std::min(in.receiveUs, lastNewUs_ + duplicates_ * frameUs_);
            ++counters_.synthesized;
        } else {
            candidate = fallback(in);
        }
        return monotonic(candidate);
    }

    const Counters &counters() const { return counters_; }
    int64_t meanLatencyUs() const
    {
        return counters_.present ? counters_.sumLatencyUs / int64_t(counters_.present) : 0;
    }

private:
    static constexpr int64_t kNone = INT64_MIN;

    int64_t monotonic(int64_t candidate)
    {
        if (lastStampUs_ != kNone && candidate <= lastStampUs_) {
            candidate = lastStampUs_ + 1;
            ++counters_.clamped;
        }
        lastStampUs_ = candidate;
        return candidate;
    }

    int64_t fallback(const Input &in)
    {
        ++counters_.fallback;
        // A later new image restarts duplicate synthesis from its own present time.
        lastNewUs_ = kNone;
        duplicates_ = 0;
        return in.receiveUs;
    }

    int64_t frameUs_;
    int64_t maxAgeUs_;
    int64_t futureToleranceUs_;
    Counters counters_;
    int64_t lastStampUs_ = kNone;
    int64_t lastNewUs_ = kNone;
    int64_t duplicates_ = 0;
    bool afterHold_ = false;
};

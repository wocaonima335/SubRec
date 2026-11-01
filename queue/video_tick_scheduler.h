#pragma once

#include "media_clock.h"
#include <functional>

extern "C" {
#include <libavutil/mathematics.h>
}

struct TickDecision {
    enum class Kind { Accept, SlotCollision, Lagged, NotConfigured, InvalidTime };
    Kind kind = Kind::NotConfigured;
    int64_t tick = AV_NOPTS_VALUE;
    int64_t skippedTicks = 0;
    // Lagged accepts the current frame, leaving a gap; it does not request copies.
    constexpr bool accepted() const noexcept {
        return kind == Kind::Accept || kind == Kind::Lagged;
    }
};

struct VideoTickCounters {
    uint64_t frames = 0;         // Accepted captured frames, including Lagged.
    uint64_t slotCollisions = 0;
    uint64_t laggedTicks = 0;    // Sum of skippedTicks for accepted capture frames.
    uint64_t holdoverTicks = 0;  // Successfully submitted repeated frames only.
};

// Declaration-only scheduler; implementation belongs in video_tick_scheduler.cpp.
// SINGLE OWNER THREAD: serialize configure/reset/beginSegment/decide, clock-driven
// hold requests and counter reads on the encoder/scheduler worker. Post clock or
// capture-loss notifications to that worker; never invoke callbacks concurrently
// with capture encoding. No locks, threads, timers, AVFrame or GPU resources are
// owned here. Caller owns the last-frame reference and encoder/surface lifetime.
class VideoTickScheduler {
public:
    // true means the owner successfully submitted a repeat of its last frame
    // with encoder PTS=tick. mediaUs is that tick's scheduled session media time
    // (segment baseUs + rescale(tick-baseTick, inverse fps, 1/1e6)), NOT a new
    // capture timestamp. false means no submission (e.g. no previous frame).
    // Callback must not throw or reenter the scheduler; captured resources must
    // outlive registration. Fatal encoding errors are reported by the owner.
    using HoldFrameCallback = std::function<bool(int64_t tick, int64_t mediaUs)>;

    VideoTickScheduler();
    ~VideoTickScheduler();
    VideoTickScheduler(const VideoTickScheduler &) = delete;
    VideoTickScheduler &operator=(const VideoTickScheduler &) = delete;

    // Positive numerator/denominator required. false leaves all state unchanged.
    // Success stores fps and resets session state, retaining callback registration.
    // Until configured, decide returns NotConfigured and hold requests do nothing.
    bool configure(AVRational fps = kRecordFrameRate);
    // Keeps fps/callback, clears counters, sets lastTick=-1, baseTick=0, baseUs=0.
    // This initial origin preserves a slow first capture's positive start offset
    // for the MP4 edit list. Do NOT re-anchor to the first capture at startup.
    void reset();

    // Resume only: call with the first post-resume frame's mediaUs BEFORE decide
    // on that frame. Sets baseTick=lastTick+1 and baseUs=firstFrameMediaUs; does
    // not consume a tick or clear counters. Owner forces the resume frame to I.
    // Stop clock-driven holds during pause and until this new anchor is installed.
    // false on unconfigured/negative/NOPTS input or backwards segment anchor,
    // with no state change. Capture restoration WITHOUT pause keeps the old anchor.
    bool beginSegment(int64_t firstFrameMediaUs);

    // Input = MediaClock::mediaUs(frameCaptureUs), not delayed audio output time.
    // n = baseTick + av_rescale_rnd(mediaUs-baseUs, fps.num,
    //                              int64_t(fps.den)*1000000, AV_ROUND_NEAR_INF).
    // Round nearest, ties away from zero; recompute from anchor, NEVER accumulate
    // rounded microsecond durations. Encoder time_base=av_inv_q(fps), PTS=n.
    // n<=lastTick: SlotCollision, count it, no advancement; tick=n, skipped=0.
    // n==lastTick+1: Accept. n>lastTick+1: Lagged, skipped=n-lastTick-1.
    // Accept/Lagged consume n immediately and increment frames (no rollback on
    // encoder failure); holdoverTicks are not included in frames. InvalidTime
    // (negative/NOPTS/before baseUs or overflow) has tick=NOPTS, no state changes.
    TickDecision decide(int64_t mediaUs);

    // For an already frame-rate-paced capture source (ddagrab): retain the next
    // consecutive tick while its timestamp error is less than one frame. This
    // hysteresis prevents receive-time jitter around a half-frame boundary from
    // alternating gaps and collisions. Larger errors still skip/drop to track
    // the session clock; duplicate/backwards capture times are rejected.
    // First capture and firstAfterResume use nearest SESSION tick, preserving
    // startup/resume delay without accumulating a new segment's rounding error.
    // Use instead of decide/beginSegment for the entire session. Holds share
    // lastTick and keep the session origin. Timestamp error is bounded by one
    // frame (apart from a resume forced forward to maintain monotonic PTS).
    TickDecision decidePaced(int64_t mediaUs, bool firstAfterResume = false);

    void setHoldFrameCallback(HoldFrameCallback callback);
    // Explicit stalled-capture drive; not used to fill ordinary Lagged decisions.
    // Caller requests this only while capture is stalled, session running, and
    // a valid last frame exists. Invoke regularly using MediaClock::nowMediaUs().
    // Visits consecutive ticks from lastTick+1 through the last DUE tick computed
    // from the anchor with AV_ROUND_DOWN (never submit a future tick). At most
    // maxTicks attempts per call bounds worker time; backlog remains for the next
    // call. false stops this call without consuming that tick; true advances
    // lastTick and holdoverTicks. Returns successful submission count. No callback,
    // maxTicks<=0, invalid time, not configured or no accepted frame => zero.
    // After recovery decide() shares lastTick, so a late capture may collide with
    // a successfully held tick. Neither failed holds nor backlog count as lagged.
    int64_t requestHoldover(int64_t nowMediaUs, int64_t maxTicks = 1);

    VideoTickCounters counters() const; // Read-only value snapshot, owner thread.
    int64_t lastTick() const;           // -1 before any accepted/held tick.

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

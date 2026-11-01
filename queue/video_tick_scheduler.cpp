#include "video_tick_scheduler.h"
#include <algorithm>
#include <limits>
#include <utility>

struct VideoTickScheduler::Impl {
    AVRational fps{0,1};
    int64_t baseTick = 0, baseUs = 0, last = -1;
    int64_t lastCaptureUs = AV_NOPTS_VALUE;
    VideoTickCounters counts;
    HoldFrameCallback hold;
    bool configured() const { return fps.num > 0 && fps.den > 0; }
    int64_t tickAt(int64_t us, AVRounding rounding) const {
        if (us < baseUs || us == AV_NOPTS_VALUE) return AV_NOPTS_VALUE;
        const int64_t delta = av_rescale_rnd(us - baseUs, fps.num, int64_t(fps.den)*1000000, rounding);
        if (delta < 0 || delta > INT64_MAX-baseTick) return AV_NOPTS_VALUE;
        return baseTick + delta;
    }
};
VideoTickScheduler::VideoTickScheduler() : impl_(std::make_unique<Impl>()) {}
VideoTickScheduler::~VideoTickScheduler() = default;
bool VideoTickScheduler::configure(AVRational fps) {
    if (fps.num <= 0 || fps.den <= 0) return false;
    impl_->fps = fps; reset(); return true;
}
void VideoTickScheduler::reset() {
    impl_->baseTick = impl_->baseUs = 0; impl_->last = -1; impl_->counts = {};
    impl_->lastCaptureUs = AV_NOPTS_VALUE;
}
bool VideoTickScheduler::beginSegment(int64_t us) {
    if (!impl_->configured() || us < impl_->baseUs || impl_->last == INT64_MAX) return false;
    impl_->baseTick = impl_->last + 1; impl_->baseUs = us; return true;
}
TickDecision VideoTickScheduler::decide(int64_t us) {
    if (!impl_->configured()) return {};
    const auto tick = impl_->tickAt(us, AV_ROUND_NEAR_INF);
    if (tick == AV_NOPTS_VALUE) return {TickDecision::Kind::InvalidTime, tick, 0};
    if (tick <= impl_->last) {
        ++impl_->counts.slotCollisions; return {TickDecision::Kind::SlotCollision, tick, 0};
    }
    const auto skipped = tick - (impl_->last + 1);
    impl_->last = tick; ++impl_->counts.frames; impl_->counts.laggedTicks += skipped;
    return {skipped ? TickDecision::Kind::Lagged : TickDecision::Kind::Accept, tick, skipped};
}
TickDecision VideoTickScheduler::decidePaced(int64_t us, bool firstAfterResume) {
    if (!impl_->configured()) return {};
    auto tick = impl_->tickAt(us, AV_ROUND_NEAR_INF);
    if (tick == AV_NOPTS_VALUE || impl_->last == INT64_MAX)
        return {TickDecision::Kind::InvalidTime, AV_NOPTS_VALUE, 0};
    if (impl_->lastCaptureUs != AV_NOPTS_VALUE && us <= impl_->lastCaptureUs) {
        ++impl_->counts.slotCollisions;
        return {TickDecision::Kind::SlotCollision, tick, 0};
    }
    impl_->lastCaptureUs = us;
    const auto next = impl_->last + 1;
    if (firstAfterResume) {
        // MediaClock has already removed exactly the same pause as audio.
        // Never compress the uncaptured time on either side of that pause.
        tick = std::max(next, tick);
    } else if (impl_->last >= 0) {
        const auto nextUs = av_rescale_q(next, av_inv_q(impl_->fps), AVRational{1,1000000});
        const auto periodUs = av_rescale_q(1, av_inv_q(impl_->fps), AVRational{1,1000000});
        if (nextUs >= 0 && us - nextUs > -periodUs && us - nextUs < periodUs)
            tick = next;
    }
    if (tick <= impl_->last) {
        ++impl_->counts.slotCollisions;
        return {TickDecision::Kind::SlotCollision, tick, 0};
    }
    const auto skipped = tick - next;
    impl_->last = tick; ++impl_->counts.frames; impl_->counts.laggedTicks += skipped;
    return {skipped ? TickDecision::Kind::Lagged : TickDecision::Kind::Accept, tick, skipped};
}
void VideoTickScheduler::setHoldFrameCallback(HoldFrameCallback callback) { impl_->hold = std::move(callback); }
int64_t VideoTickScheduler::requestHoldover(int64_t us, int64_t maxTicks) {
    if (!impl_->configured() || !impl_->hold || impl_->last < 0 || maxTicks <= 0) return 0;
    const auto due = impl_->tickAt(us, AV_ROUND_DOWN);
    int64_t count = 0;
    while (impl_->last < due && count < maxTicks) {
        const auto tick = impl_->last + 1;
        const auto delta = av_rescale_q(tick - impl_->baseTick, av_inv_q(impl_->fps), AVRational{1,1000000});
        if (delta < 0 || delta > INT64_MAX-impl_->baseUs || !impl_->hold(tick, impl_->baseUs+delta)) break;
        impl_->last = tick; ++impl_->counts.holdoverTicks; ++count;
    }
    return count;
}
VideoTickCounters VideoTickScheduler::counters() const { return impl_->counts; }
int64_t VideoTickScheduler::lastTick() const { return impl_->last; }

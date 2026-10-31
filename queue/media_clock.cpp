#include "media_clock.h"
#include <algorithm>
#include <limits>
#include <mutex>
#include <vector>
extern "C" {
#include <libavutil/time.h>
}

struct MediaClock::Impl {
    mutable std::mutex mutex;
    int64_t origin = AV_NOPTS_VALUE, lastTransition = AV_NOPTS_VALUE, total = 0;
    struct Pause { int64_t begin, end; };
    std::vector<Pause> pauses;
    bool paused() const { return !pauses.empty() && pauses.back().end == AV_NOPTS_VALUE; }
    int64_t media(int64_t t) const {
        if (origin == AV_NOPTS_VALUE || t == AV_NOPTS_VALUE) return AV_NOPTS_VALUE;
        if (t <= origin) return 0;
        // Absolute monotonic timestamps are nonnegative in production. Saturate
        // pathological synthetic spans rather than invoking signed overflow.
        const auto span = uint64_t(t) - uint64_t(origin);
        if (span > uint64_t(INT64_MAX)) return AV_NOPTS_VALUE;
        int64_t result = static_cast<int64_t>(span);
        for (const auto &p : pauses) {
            if (p.begin >= t) break;
            result -= std::min(t, p.end == AV_NOPTS_VALUE ? t : p.end) - p.begin;
        }
        return result;
    }
};
MediaClock::MediaClock() : impl_(std::make_unique<Impl>()) {}
MediaClock::~MediaClock() = default;
void MediaClock::start(int64_t t0Us) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->origin = impl_->lastTransition = t0Us;
    impl_->total = 0; impl_->pauses.clear();
}
void MediaClock::pause(int64_t tUs) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->origin == AV_NOPTS_VALUE || tUs < impl_->lastTransition || impl_->paused()) return;
    impl_->pauses.push_back({tUs, AV_NOPTS_VALUE}); impl_->lastTransition = tUs;
}
void MediaClock::resume(int64_t tUs) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->paused() || tUs < impl_->lastTransition) return;
    const uint64_t duration = uint64_t(tUs) - uint64_t(impl_->pauses.back().begin);
    if (duration > uint64_t(INT64_MAX - impl_->total)) return;
    impl_->total += static_cast<int64_t>(duration);
    impl_->pauses.back().end = tUs; impl_->lastTransition = tUs;
}
int64_t MediaClock::mediaUs(int64_t tUs) const {
    std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->media(tUs);
}
int64_t MediaClock::nowMediaUs() const {
    std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->media(av_gettime_relative());
}
int64_t MediaClock::pausedTotalUs() const {
    std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->total;
}
bool MediaClock::isPaused() const {
    std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->paused();
}

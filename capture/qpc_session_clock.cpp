#include "qpc_session_clock.h"

#include <algorithm>
#include <cmath>
#include <limits>

extern "C" {
#include <libavutil/time.h>
}

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
constexpr int64_t kNoValue = std::numeric_limits<int64_t>::min();   // AV_NOPTS_VALUE
}

QpcSessionClock &QpcSessionClock::instance()
{
    static QpcSessionClock clock;
    return clock;
}

void QpcSessionClock::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    anchors_.clear();
    total_ = 0;
    hasFirst_ = false;
    firstOffsetUs_ = 0;
    slope_ = 1;
    intercept_ = maxResidual_ = 0;
    lastRefreshSessionUs_ = 0;
}

int64_t QpcSessionClock::ticksTo100ns(int64_t ticks, int64_t frequency)
{
    if (frequency <= 0)
        return kNoValue;
    return ticks / frequency * 10000000 + ticks % frequency * 10000000 / frequency;
}

void QpcSessionClock::addAnchor(const Anchor &anchor)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t qpcUs = anchor.qpc100ns / 10;
    if (!hasFirst_) {
        hasFirst_ = true;
        firstOffsetUs_ = anchor.sessionUs - qpcUs;
    }
    anchors_.push_back(anchor);
    while (anchors_.size() > kWindow)
        anchors_.pop_front();
    ++total_;
    lastRefreshSessionUs_ = anchor.sessionUs;
    fitLocked();
}

void QpcSessionClock::fitLocked()
{
    // Relative coordinates keep the doubles exact enough for hours of recording.
    baseX_ = anchors_.front().qpc100ns / 10;
    baseY_ = anchors_.front().sessionUs;
    const size_t n = anchors_.size();
    double sx = 0, sy = 0;
    for (const Anchor &a : anchors_) {
        sx += double(a.qpc100ns / 10 - baseX_);
        sy += double(a.sessionUs - baseY_);
    }
    const double mx = sx / double(n), my = sy / double(n);
    double sxx = 0, sxy = 0;
    for (const Anchor &a : anchors_) {
        const double dx = double(a.qpc100ns / 10 - baseX_) - mx;
        sxy += dx * (double(a.sessionUs - baseY_) - my);
        sxx += dx * dx;
    }
    // Less than ~1 s of spread cannot resolve ppm: assume equal rates.
    slope_ = sxx > 1e12 ? sxy / sxx : 1.0;
    const double maxDeviation = kMaxSlopePpm * 1e-6;
    slope_ = std::clamp(slope_, 1.0 - maxDeviation, 1.0 + maxDeviation);
    intercept_ = my - slope_ * mx;
    maxResidual_ = 0;
    for (const Anchor &a : anchors_) {
        const double fitted = intercept_ + slope_ * double(a.qpc100ns / 10 - baseX_);
        maxResidual_ = std::max(maxResidual_, std::abs(double(a.sessionUs - baseY_) - fitted));
    }
}

int64_t QpcSessionClock::toSessionUs(int64_t qpc100ns) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (anchors_.empty() || qpc100ns == kNoValue)
        return kNoValue;
    const double x = double(qpc100ns / 10 - baseX_);
    return baseY_ + static_cast<int64_t>(std::llround(intercept_ + slope_ * x));
}

QpcSessionClock::Stats QpcSessionClock::stats() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    Stats s;
    s.anchors = total_;
    if (anchors_.empty())
        return s;
    s.slopePpm = (slope_ - 1.0) * 1e6;
    s.maxResidualUs = maxResidual_;
    const Anchor &last = anchors_.back();
    s.driftUs = (last.sessionUs - last.qpc100ns / 10) - firstOffsetUs_;
    s.lastUncertaintyUs = last.uncertaintyUs;
    return s;
}

void QpcSessionClock::setRefreshIntervalUs(int64_t intervalUs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    refreshIntervalUs_ = std::max<int64_t>(1000, intervalUs);
}

bool QpcSessionClock::refresh(bool force)
{
#ifdef _WIN32
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!force && !anchors_.empty() &&
            av_gettime_relative() - lastRefreshSessionUs_ < refreshIntervalUs_)
            return false;
    }
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    int64_t bestGap = std::numeric_limits<int64_t>::max(), bestMid = 0, bestSession = 0;
    for (int i = 0; i < 3; ++i) {
        LARGE_INTEGER before, after;
        QueryPerformanceCounter(&before);
        const int64_t session = av_gettime_relative();
        QueryPerformanceCounter(&after);
        const int64_t gap = after.QuadPart - before.QuadPart;
        if (gap < bestGap) {
            bestGap = gap;
            bestMid = before.QuadPart + gap / 2;
            bestSession = session;
        }
    }
    Anchor anchor;
    anchor.qpc100ns = ticksTo100ns(bestMid, frequency.QuadPart);
    anchor.sessionUs = bestSession;
    anchor.uncertaintyUs = ticksTo100ns(bestGap, frequency.QuadPart) / 20;
    addAnchor(anchor);
    return true;
#else
    (void)force;
    return false;
#endif
}

int64_t QpcSessionClock::from100ns(int64_t qpc100ns)
{
    refresh();
    return toSessionUs(qpc100ns);
}

int64_t QpcSessionClock::fromQpcTicks(int64_t ticks)
{
#ifdef _WIN32
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return from100ns(ticksTo100ns(ticks, frequency.QuadPart));
#else
    (void)ticks;
    return kNoValue;
#endif
}

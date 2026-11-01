#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

// Maps Windows QueryPerformanceCounter readings onto the session clock, i.e. the
// av_gettime_relative() microsecond domain used by MediaClock (queue/media_clock.h).
//
// Event timestamps reported by Windows are QPC based: WASAPI GetBuffer() returns
// QPC in 100 ns units, DXGI Desktop Duplication LastPresentTime in raw QPC ticks.
// av_gettime_relative() is not guaranteed to run on QPC (FFmpeg falls back to the
// system time when clock_gettime is unavailable), so a one-shot offset is not
// enough. The clock keeps anchor pairs (QPC, session time), refreshed at most every
// refreshIntervalUs, and maps with a least-squares line over the most recent
// anchors. Slope, residual and drift are exposed so field logs show whether the
// two clocks diverge.
//
// Process-wide: both clocks are process-wide, so all capture threads share one
// mapping. All methods are thread-safe. The mapping is close to, but not strictly,
// monotonic across refits; consumers that need strict order enforce it themselves.
class QpcSessionClock
{
public:
    struct Anchor {
        int64_t qpc100ns = 0;
        int64_t sessionUs = 0;
        int64_t uncertaintyUs = 0;  // half the QPC read bracket
    };
    struct Stats {
        uint64_t anchors = 0;
        double slopePpm = 0;        // session clock rate relative to QPC, minus 1, in ppm
        double maxResidualUs = 0;   // worst anchor deviation from the current fit
        int64_t driftUs = 0;        // offset change between the first and newest anchor
        int64_t lastUncertaintyUs = 0;
    };

    static constexpr size_t kWindow = 16;
    static constexpr int64_t kDefaultRefreshIntervalUs = 10 * 1000000LL;
    static constexpr double kMaxSlopePpm = 1000;

    static QpcSessionClock &instance();

    // ---- pure logic (also used directly by tests) ----
    void reset();
    void addAnchor(const Anchor &anchor);
    // AV_NOPTS_VALUE (INT64_MIN) before the first anchor.
    int64_t toSessionUs(int64_t qpc100ns) const;
    Stats stats() const;
    static int64_t ticksTo100ns(int64_t ticks, int64_t frequency);

    // ---- Windows (no-ops elsewhere) ----
    // Takes a new anchor (best of three bracketed reads) if none exists, force is
    // set, or refreshIntervalUs elapsed since the last one. Returns true if added.
    bool refresh(bool force = false);
    // refresh() + conversion; AV_NOPTS_VALUE when no anchor can be taken.
    int64_t from100ns(int64_t qpc100ns);
    int64_t fromQpcTicks(int64_t ticks);
    void setRefreshIntervalUs(int64_t intervalUs);

private:
    void fitLocked();

    mutable std::mutex mutex_;
    std::deque<Anchor> anchors_;
    uint64_t total_ = 0;
    int64_t firstOffsetUs_ = 0;
    bool hasFirst_ = false;
    // Fit: sessionUs = baseY + (qpcUs - baseX) * slope + intercept
    int64_t baseX_ = 0, baseY_ = 0;
    double slope_ = 1, intercept_ = 0, maxResidual_ = 0;
    int64_t refreshIntervalUs_ = kDefaultRefreshIntervalUs;
    int64_t lastRefreshSessionUs_ = 0;
};

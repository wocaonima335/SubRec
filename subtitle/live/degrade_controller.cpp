#include "subtitle/live/degrade_controller.h"

#include <algorithm>

namespace subtitle {
namespace {
bool sustained(bool condition, int64_t now, int holdMs, std::optional<int64_t> &since)
{
    if (!condition) {
        since.reset();
        return false;
    }
    if (!since) since = now;
    return now - *since >= holdMs;
}
} // namespace

DegradeController::DegradeController(DegradeThresholds thresholds) : m_thresholds(thresholds) {}

void DegradeController::clearSustain()
{
    m_lagSince.reset();
    m_rtfSince.reset();
    m_cpuSince.reset();
    m_recoverySince.reset();
}

bool DegradeController::recordingUnhealthy(const LoadSample &sample)
{
    // Counter resets must establish a new baseline, never unsigned-wrap into drops.
    if (!m_healthWindow.empty()) {
        const auto &previous = m_healthWindow.back();
        if (sample.health.videoFrames < previous.health.videoFrames ||
            sample.health.videoTickDrops < previous.health.videoTickDrops)
            m_healthWindow.clear();
        else if (sample.nowMs == previous.nowMs)
            m_healthWindow.pop_back();
    }
    m_healthWindow.push_back(sample);
    const int64_t cutoff = sample.nowMs - m_thresholds.recWindowMs;
    // Keep the snapshot straddling the start of the rolling window.
    while (m_healthWindow.size() > 1 && m_healthWindow[1].nowMs <= cutoff)
        m_healthWindow.erase(m_healthWindow.begin());
    if (m_healthWindow.size() < 2 || m_healthWindow.front().nowMs > cutoff)
        return false;

    const auto &first = m_healthWindow[0];
    const auto &second = m_healthWindow[1];
    const long double fraction = static_cast<long double>(cutoff - first.nowMs) /
                                 (second.nowMs - first.nowMs);
    // Interpolate cumulative counters only at an unsampled window boundary.
    const long double frames = static_cast<long double>(sample.health.videoFrames - first.health.videoFrames) -
        fraction * (second.health.videoFrames - first.health.videoFrames);
    const long double drops = static_cast<long double>(sample.health.videoTickDrops - first.health.videoTickDrops) -
        fraction * (second.health.videoTickDrops - first.health.videoTickDrops);
    long double cpuMs = 0;
    for (std::size_t i = 1; i < m_healthWindow.size(); ++i) {
        const auto &left = m_healthWindow[i - 1];
        const auto &right = m_healthWindow[i];
        // A sampled CPU value holds until the next sample; average over the same window.
        cpuMs += left.hostCpuCores * (right.nowMs - std::max(cutoff, left.nowMs));
    }
    return frames > 0 && drops / frames > m_thresholds.recDropFrameRatio &&
           cpuMs / m_thresholds.recWindowMs > m_thresholds.recHostCpuCores;
}

std::optional<std::pair<DegradeLevel, std::string>> DegradeController::tick(const LoadSample &s)
{
    // Stale observations cannot rewind a sustain timer or the health window.
    if (m_lastSampleMs && s.nowMs < *m_lastSampleMs) return std::nullopt;
    m_lastSampleMs = s.nowMs;
    if (m_anchorForcedTransition) {
        m_lastTransitionMs = s.nowMs;
        m_anchorForcedTransition = false;
    }
    const bool rec = recordingUnhealthy(s);
    // Failed/deferred jobs only leave these states through an explicit forceLevel.
    if (m_level == DegradeLevel::L4Failed || m_level == DegradeLevel::L3Deferred)
        return std::nullopt;

    auto change = [&](DegradeLevel level, const char *reason) {
        m_level = level;
        m_reason = reason;
        m_lastTransitionMs = s.nowMs;
        clearSustain();
        return std::make_optional(std::make_pair(m_level, m_reason));
    };
    if (rec) return change(DegradeLevel::L3Deferred, "rec");
    if (m_level == DegradeLevel::L2CatchUp && s.lagSec > m_thresholds.l2l3LagSec)
        return change(DegradeLevel::L3Deferred, "lagL2");

    const auto &t = m_thresholds;
    const bool intervalReady = !m_lastTransitionMs ||
        s.nowMs - *m_lastTransitionMs >= t.minTransitionIntervalMs;
    if (m_level == DegradeLevel::L0Live) {
        const bool lag = sustained(s.lagSec > t.l0l1LagSec, s.nowMs, t.l0l1LagHoldMs, m_lagSince);
        const bool rtf = sustained(s.rtfEwma > t.l0l1Rtf, s.nowMs, t.l0l1RtfHoldMs, m_rtfSince);
        const bool cpu = sustained(s.sysCpu > t.l0l1SysCpu, s.nowMs, t.l0l1SysCpuHoldMs, m_cpuSince);
        if (intervalReady && (lag || rtf || cpu))
            return change(DegradeLevel::L1FinalOnly, lag ? "lag" : rtf ? "rtf" : "sysCpu");
    } else if (m_level == DegradeLevel::L1FinalOnly) {
        const bool lag = sustained(s.lagSec > t.l1l2LagSec, s.nowMs, t.l1l2LagHoldMs, m_lagSince);
        const bool rtf = sustained(s.rtfEwma > t.l1l2Rtf, s.nowMs, t.l1l2RtfHoldMs, m_rtfSince);
        const bool recover = sustained(s.lagSec < t.l1l0LagSec && s.rtfEwma < t.l1l0Rtf &&
            s.sysCpu < t.l1l0SysCpu, s.nowMs, t.l1l0HoldMs, m_recoverySince);
        if (intervalReady && (lag || rtf))
            return change(DegradeLevel::L2CatchUp, lag ? "lag" : "rtf");
        if (intervalReady && recover) return change(DegradeLevel::L0Live, "lag");
    } else if (m_level == DegradeLevel::L2CatchUp) {
        const bool recover = sustained(s.lagSec < t.l2l1LagSec, s.nowMs, t.l2l1HoldMs, m_recoverySince);
        if (intervalReady && recover) return change(DegradeLevel::L1FinalOnly, "lag");
    }
    return std::nullopt;
}

void DegradeController::forceLevel(DegradeLevel level, std::string reason)
{
    m_level = level;
    m_reason = std::move(reason);
    clearSustain();
    // There is no clock argument here: use the last injected time, or anchor on first tick.
    m_lastTransitionMs = m_lastSampleMs;
    m_anchorForcedTransition = !m_lastSampleMs;
}

DegradeLevel DegradeController::level() const { return m_level; }
const std::string &DegradeController::reason() const { return m_reason; }

} // namespace subtitle

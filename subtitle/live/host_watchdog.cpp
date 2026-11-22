#include "subtitle/live/host_watchdog.h"
#include "subtitle/host/asr_protocol.h"

#include <algorithm>

namespace subtitle {

HostWatchdog::HostWatchdog(WatchdogConfig config, IClock &clock)
    : m_config(config), m_clock(clock),
      m_nextBackoffMs(std::min(config.backoffBaseMs, config.backoffMaxMs)) {}

void HostWatchdog::onProcessStarted()
{
    m_running = true;
    m_lastHbMs = m_clock.nowMs();
    m_loading = false;
    m_busy = false;
    m_backend = Backend::Auto;
}

void HostWatchdog::onHeartbeat(int64_t /*seq*/)
{
    // Receipt is liveness; seq is diagnostic and may restart with each process.
    m_lastHbMs = m_clock.nowMs();
}

void HostWatchdog::onBusy(int64_t begin, int64_t end, int64_t samples)
{
    m_busy = true;
    m_busyStartedMs = m_clock.nowMs();
    m_begin16k = begin;
    m_end16k = end;
    m_decodeTimeoutMs = std::max(static_cast<double>(m_config.minDecodeTimeoutMs),
        static_cast<double>(samples) / kAsrSampleRate * m_config.decodeRtfLimit * 1000.0);
}

void HostWatchdog::onIdle() { m_busy = false; }
void HostWatchdog::onLoadStarted()
{
    m_loading = true;
    m_loadStartedMs = m_clock.nowMs();
    m_busy = false;
}
void HostWatchdog::onLoaded() { m_loading = false; }
void HostWatchdog::setBackend(Backend active) { m_backend = active; }

HostWatchdog::Verdict HostWatchdog::poll(int64_t nowMs) const
{
    if (!m_running) return Verdict::Ok;
    // Report the specific operation deadline first if multiple deadlines have expired.
    if (m_loading && nowMs - m_loadStartedMs >= m_config.loadTimeoutMs)
        return Verdict::KillLoadTimeout;
    if (m_busy && nowMs - m_busyStartedMs >= m_decodeTimeoutMs)
        return Verdict::KillDecodeTimeout;
    if (nowMs - m_lastHbMs >= m_config.hbTimeoutMs) return Verdict::KillHung;
    return Verdict::Ok;
}

HostWatchdog::RestartPlan HostWatchdog::onExit(int exitCode, bool crashed, int64_t resume16k)
{
    const int64_t now = m_clock.nowMs();
    const bool failed = crashed || exitCode != proto::kExitOk;
    const bool loadTimedOut = m_loading && now - m_loadStartedMs >= m_config.loadTimeoutMs;
    const bool noRetry = exitCode == proto::kExitCpuUnsupported ||
        static_cast<uint32_t>(exitCode) == proto::kStatusIllegalInstruction ||
        exitCode == proto::kExitRuntimeMissing || exitCode == proto::kExitEngineLoadFailed ||
        exitCode == proto::kExitVersionMismatch;
    if (failed && (m_backend == Backend::Vulkan || m_backend == Backend::Cuda))
        m_forceCpu = true;

    RestartPlan plan{false, 0, false, resume16k, m_forceCpu};
    if (failed && m_busy) {
        plan.poison = m_hasFailedInterval && m_begin16k == m_failedBegin16k && m_end16k == m_failedEnd16k;
        if (plan.poison) plan.skipTo16k = m_end16k;
        m_hasFailedInterval = true;
        m_failedBegin16k = m_begin16k;
        m_failedEnd16k = m_end16k;
    } else {
        m_hasFailedInterval = false;
    }
    m_running = false;
    m_loading = false;
    m_busy = false;
    m_backend = Backend::Auto;

    if (!failed || noRetry || loadTimedOut) return plan;
    // Sliding window is (now - window, now]; an attempt expires exactly at 10 minutes.
    while (!m_restartTimesMs.empty() && now - m_restartTimesMs.front() >= m_config.restartWindowMs)
        m_restartTimesMs.pop_front();
    if (static_cast<int>(m_restartTimesMs.size()) >= m_config.maxRestarts) return plan;
    plan.allowed = true;
    plan.delayMs = m_nextBackoffMs;
    m_restartTimesMs.push_back(now);
    // Lifetime-of-job backoff is independent of the sliding restart budget; saturate before multiplying.
    m_nextBackoffMs = m_nextBackoffMs >= m_config.backoffMaxMs / 2 + m_config.backoffMaxMs % 2
        ? m_config.backoffMaxMs : m_nextBackoffMs * 2;
    return plan;
}

void HostWatchdog::resetForJob()
{
    m_restartTimesMs.clear();
    m_hasFailedInterval = false;
    m_nextBackoffMs = std::min(m_config.backoffBaseMs, m_config.backoffMaxMs);
    // A new job may use the same live process. Preserve its timers/backend and session CPU fallback.
}

} // namespace subtitle

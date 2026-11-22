// M0 桩：subtitle_live 中归工作包 W 的部分（degrade_controller / host_watchdog）。
// W 实现时拆成 degrade_controller.cpp / host_watchdog.cpp 并删除本文件。桩行为：不迁移级别、不建议重启。

#include "subtitle/live/degrade_controller.h"
#include "subtitle/live/host_watchdog.h"

#include <utility>

namespace subtitle {

// ---- DegradeController（W）----
DegradeController::DegradeController(DegradeThresholds t) : m_thresholds(t) {}
std::optional<std::pair<DegradeLevel, std::string>> DegradeController::tick(const LoadSample &) { return std::nullopt; }
void DegradeController::forceLevel(DegradeLevel level, std::string reason)
{
    m_level = level;
    m_reason = std::move(reason);
}
DegradeLevel DegradeController::level() const { return m_level; }
const std::string &DegradeController::reason() const { return m_reason; }

// ---- HostWatchdog（W）----
HostWatchdog::HostWatchdog(WatchdogConfig config, IClock &clock) : m_config(config), m_clock(clock) {}
void HostWatchdog::onHeartbeat(int64_t) { m_lastHbMs = m_clock.nowMs(); }
void HostWatchdog::onBusy(int64_t, int64_t, int64_t) {}
void HostWatchdog::onIdle() {}
void HostWatchdog::onLoadStarted() {}
void HostWatchdog::onLoaded() {}
HostWatchdog::Verdict HostWatchdog::poll(int64_t) const { return Verdict::Ok; }
HostWatchdog::RestartPlan HostWatchdog::onExit(int, bool, int64_t resume16k)
{
    return RestartPlan{false, 0, false, resume16k, false};
}
void HostWatchdog::onProcessStarted() { m_lastHbMs = m_clock.nowMs(); }
void HostWatchdog::setBackend(Backend) {}
void HostWatchdog::resetForJob() { m_restartTimesMs.clear(); }

} // namespace subtitle

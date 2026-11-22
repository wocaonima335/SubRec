#pragma once
// M0 冻结：宿主看门狗 HostWatchdog（design-live-v2.md §7.3、§7.4、§7.5、§10.3 W）。
// 接口归属：M0；实现归工作包 W。CMake 目标 subtitle_live（纯 C++17，IClock 可注入）。
//
// 检测（poll）
// - KillHung：距最后一次 hb ≥ hbTimeoutMs（5 s）。onLoadStarted 之前（进程刚启动）以启动时刻计。
// - KillDecodeTimeout：onBusy 之后未见 onIdle，且已过 max(minDecodeTimeoutMs, samples/16000 × decodeRtfLimit × 1000)。
// - KillLoadTimeout：onLoadStarted 之后 loadTimeoutMs（60 s）内未见 onLoaded。
// poll 为 const：只判定，不改状态；调用方 kill 后由 onExit 推进状态。
//
// 重启计划（onExit，§7.4）
// - 退避：第 k 次重启前等待 min(backoffBaseMs × 2^(k−1), backoffMaxMs)，即 1/2/4/8/16 s（上限 30 s）。
// - 同一 Job 在 restartWindowMs（10 min）内最多 maxRestarts（5）次；超出 → allowed=false（调用方 L3，码 708）。
// - 毒段：崩溃/超时时在途区间（最近一次 onBusy 的 [b16k, e16k)）与上一次相同 → poison=true、skipTo16k=e16k。
//   未在解码中退出时 poison=false、skipTo16k=resume16k。
// - 不重试的退出码（proto::kExitCpuUnsupported / kStatusIllegalInstruction、kExitRuntimeMissing、
//   kExitEngineLoadFailed、kExitVersionMismatch）→ allowed=false（调用方置 unavailable）。
//   HostMissing / HostBlocked 是启动失败，不经 onExit，由 AsrHostClient 直接报告。
// - OOM（kExitOutOfMemory）：allowed=true，调用方据退出码先 L1 再 L3（§7.5）；W 只给退避。
// - GPU 后端运行中崩溃 → forceCpu=true（本会话内不再试 GPU，M2 精修用）。
// 线程：只在 SubtitleJobRunner 线程使用。

#include "subtitle/subtitle_clock.h"
#include "subtitle/subtitle_types.h"

#include <cstdint>
#include <deque>

namespace subtitle {

struct WatchdogConfig { int hbTimeoutMs = 5000; double decodeRtfLimit = 3.0; int minDecodeTimeoutMs = 10000;
                        int loadTimeoutMs = 60000; int maxRestarts = 5; int restartWindowMs = 600000;
                        int backoffBaseMs = 1000; int backoffMaxMs = 30000; };

class HostWatchdog {
public:
    HostWatchdog(WatchdogConfig, IClock &);
    void onHeartbeat(int64_t seq);  void onBusy(int64_t b16k, int64_t e16k, int64_t samples);
    void onIdle();                  void onLoadStarted(); void onLoaded();
    enum class Verdict { Ok, KillHung, KillDecodeTimeout, KillLoadTimeout };
    Verdict poll(int64_t nowMs) const;
    struct RestartPlan { bool allowed; int delayMs; bool poison; int64_t skipTo16k; bool forceCpu; };
    RestartPlan onExit(int exitCode, bool crashed, int64_t resume16k);

    // ---- M0 补齐 ----
    void onProcessStarted();                        // 新进程启动：hb 计时从此刻开始
    void setBackend(Backend active);                // loaded 回报的实际后端；GPU 崩溃时 forceCpu
    void resetForJob();                             // 新 Job：清空重启计数与毒段记录

private:                                            // 私有成员归实现方 W，可自由调整
    WatchdogConfig m_config;
    IClock &m_clock;
    int64_t m_lastHbMs = 0;
    std::deque<int64_t> m_restartTimesMs;
};

} // namespace subtitle

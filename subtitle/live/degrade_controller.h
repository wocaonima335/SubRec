#pragma once
// M0 冻结：降级状态机 DegradeController（design-live-v2.md §6.2、§6.3、§10.3 W、D20）。
// 接口归属：M0；实现归工作包 W。CMake 目标 subtitle_live（纯 C++17，无时钟依赖：时间取 LoadSample::nowMs）。
//
// 迁移规则（§6.3，阈值均为初值，PC3 校准后只改 DegradeThresholds 默认值）
//   L0→L1：lag > 6 s 持续 10 s，或 rtf > 0.5 持续 10 s，或 sysCpu > 90 % 持续 10 s
//   L1→L2：lag > 20 s 持续 15 s，或 rtf > 0.9 持续 30 s
//   任意→L3：rec 增量 > 0.2 % 帧（10 s 窗口）且同窗口子进程 CPU > 0.2 核；或 L2 下 lag > 120 s —— 立即，不等迟滞
//   L1→L0：lag < 3 s 且 rtf < 0.3 且 sysCpu < 75 % 持续 60 s
//   L2→L1：lag < 5 s 持续 60 s
//   L3→L2：不自动（本次录制内）；用户「恢复实时识别」→ forceLevel(L2)
//   任意→L4：spool 写失败 / 重启耗尽后补识别仍失败 → forceLevel(L4)
// 每次自动迁移最小间隔 10 s（迁往 L3/L4 除外）。一次 tick 最多迁移一级（L3 除外）。
// reason 为英文短标识（lag / rtf / sysCpu / rec / lagL2 / manual / restartExhausted / oom / spool），
// 用于 [ASR] level 日志；中文文案由 SubtitleService 映射。
// 线程：只在 SubtitleJobRunner 线程使用（1 Hz 定时器）。

#include "subtitle/subtitle_types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace subtitle {

struct DegradeThresholds {                  // §6.3 全部阈值、持续时间、最小间隔
    // L0 → L1
    double l0l1LagSec = 6.0;      int l0l1LagHoldMs = 10000;
    double l0l1Rtf = 0.5;         int l0l1RtfHoldMs = 10000;
    double l0l1SysCpu = 90.0;     int l0l1SysCpuHoldMs = 10000;
    // L1 → L2
    double l1l2LagSec = 20.0;     int l1l2LagHoldMs = 15000;
    double l1l2Rtf = 0.9;         int l1l2RtfHoldMs = 30000;
    // 任意 → L3（立即）
    double recDropFrameRatio = 0.002;   // videoTickDrops 增量 / videoFrames 增量（recWindowMs 窗口）
    double recHostCpuCores = 0.2;       // 同窗口子进程 CPU 下限
    int recWindowMs = 10000;
    double l2l3LagSec = 120.0;
    // 回升
    double l1l0LagSec = 3.0;      double l1l0Rtf = 0.3;     double l1l0SysCpu = 75.0;   int l1l0HoldMs = 60000;
    double l2l1LagSec = 5.0;      int l2l1HoldMs = 60000;
    // 迁移最小间隔（迁往 L3/L4 除外）
    int minTransitionIntervalMs = 10000;
};

class DegradeController {
public:
    explicit DegradeController(DegradeThresholds);
    // 每 1 s 调用；返回非空表示需要迁移
    std::optional<std::pair<DegradeLevel, std::string /*reason*/>> tick(const LoadSample &);
    void forceLevel(DegradeLevel, std::string reason);   // 崩溃耗尽、OOM、用户手动恢复
    DegradeLevel level() const;
    const std::string &reason() const;

private:                                            // 私有成员归实现方 W，可自由调整
    DegradeThresholds m_thresholds;
    DegradeLevel m_level = DegradeLevel::L0Live;
    std::string m_reason;
    std::optional<int64_t> m_lastSampleMs;
    std::optional<int64_t> m_lastTransitionMs;
    bool m_anchorForcedTransition = false;
    std::optional<int64_t> m_lagSince, m_rtfSince, m_cpuSince, m_recoverySince;
    std::vector<LoadSample> m_healthWindow;

    void clearSustain();
    bool recordingUnhealthy(const LoadSample &);
};

} // namespace subtitle

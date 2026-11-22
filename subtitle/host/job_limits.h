#pragma once
// M0 冻结：子进程 Job Object 限额（design-live-v2.md §5.2、§7.6；AsrHostClient::assignJobObject 的参数）。
// 接口归属：M0；使用方 = 工作包 D（AsrHostClient 在 Windows 上把它翻译为 JOBOBJECT_EXTENDED_LIMIT_INFORMATION
// 与 JOBOBJECT_CPU_RATE_CONTROL_INFORMATION）。纯 C++17，非 Windows 平台上 assignJobObject 直接返回 false。

#include <cstdint>

namespace subtitle {

struct JobLimits {
    bool killOnJobClose = true;                 // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    bool dieOnUnhandledException = true;        // JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION
    uint64_t processMemoryBytes = 1536ull * 1024 * 1024;   // JOB_OBJECT_LIMIT_PROCESS_MEMORY（commit）；0 = 不限
                                                // SenseVoice 实时 1.5 GB；Whisper 精修 3 GB（§5.2）
    uint32_t cpuRate = 0;                       // JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP 的 CpuRate（百分比 × 100，相对整机）
                                                // 0 = 不设；建议值 liveCpuRate(逻辑核数)

    // 实时识别的建议 CpuRate：100 % / 逻辑核数 × 1.5，钳到 [1, 10000]（§5.2）。
    static constexpr uint32_t liveCpuRate(int logicalCores) noexcept
    {
        if (logicalCores <= 1)
            return 10000;
        const uint32_t r = static_cast<uint32_t>(15000 / logicalCores);
        return r < 1 ? 1 : (r > 10000 ? 10000 : r);
    }
    static constexpr JobLimits forLive(int logicalCores) noexcept
    {
        JobLimits l;
        l.cpuRate = liveCpuRate(logicalCores);
        return l;
    }
    static constexpr JobLimits forRefine() noexcept
    {
        JobLimits l;
        l.processMemoryBytes = 3072ull * 1024 * 1024;
        return l;
    }
};

} // namespace subtitle

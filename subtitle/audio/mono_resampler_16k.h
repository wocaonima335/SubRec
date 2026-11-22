#pragma once
// M0 冻结：48 kHz → 16 kHz 单声道 FIR 重采样（design-verified.md §11.3 A）。
// 接口归属：M0；实现归工作包 A。CMake 目标 subtitle_audio（纯 C++17）。
//
// - 只在 IngestWorker 线程使用；非线程安全。
// - 输出样本号与输入严格对应：第 k 个输出样本对应输入 48k 样本 3k（已补偿群延迟），
//   保证 spool 时间轴 = 成片时间轴（§8.1）。冲激位置误差 ≤ 1 个 16k 样本（A 的验收用例）。
// - process 追加到 out16k 末尾，不清空；flush 输出滤波器尾部并复位内部状态。

#include <cstddef>
#include <vector>

namespace subtitle {

class MonoResampler16k {
public:
    MonoResampler16k();
    void process(const float *in48k, size_t n, std::vector<float> &out16k);
    void flush(std::vector<float> &out16k);
    void reset();
    // 滤波器群延迟（48k 样本数）；实现阶段由 A 确定常量值。
    static constexpr int groupDelay48k() noexcept { return kGroupDelay48k; }

private:
    static constexpr int kGroupDelay48k = 30;   // M0 占位，A 实现时改为实际值（不影响签名）
    std::vector<float> m_history;
};

} // namespace subtitle

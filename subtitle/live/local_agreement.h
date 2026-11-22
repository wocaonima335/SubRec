#pragma once
// M0 冻结：LocalAgreement-n 确认策略（design-live-v2.md §3.6、§10.3 H、D24）。
// 接口归属：M0；实现归工作包 H。CMake 目标 subtitle_live（纯 C++17，无 Qt，无时钟依赖）。
//
// 规则（§3.6.1，实现必须逐条满足，tests/subtitle_live 表驱动验证）
// 1. 比较单位为 token：比较前对每个 token 做「去标点、全角转半角、英文小写」规整；
//    标点 token 不参与一致性判定，但随其前一个 token 一起被确认。
// 2. 候选确认长度 L = 本句最近 n 次 partial 规整后 token 序列的最长公共前缀长度。
// 3. 时间护栏：再截掉「endMs > 最新 partial 的 upto16k 换算毫秒 − guardMs」的 token。
// 4. 单调：已确认长度只增不减；新一轮公共前缀短于已确认部分 → 保持不变，regressions +1。
// 5. 重置：新段、splice、宿主重启、final 后由调用方调用 reset()。
// 6. 兼容路径：partial.tokens 为空时按 UTF-8 码点比较 text，不加时间护栏（只用于测试/旧宿主）。
// 7. freeze()（L1）后 push 不再增加确认，也不清除已确认部分；tentativeText 置空。
// 8. push 的 partial.id 与当前段不同 → 视为新段，先隐式 reset(partial.id)。
//
// n 的取值：构造时钳到 [2, 3]（§3.6.2）。
// 线程：只在 SubtitleJobRunner 线程使用；非线程安全。

#include "subtitle/subtitle_types.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace subtitle {

class LocalAgreement {
public:
    LocalAgreement(int n, int guardMs);
    void reset(SegId id);                           // 新段、splice、宿主重启、final 后
    // 返回本次新增确认的 token（可能为空）；已确认部分只增不减
    std::vector<Word> push(const PartialSegment &);
    void freeze();                                  // L1：不再增长
    const ConfirmedPrefix &state() const;
    // 已确认文本与 final 对应前缀的编辑距离（按码点，L-Q1）
    int mismatchAgainst(const FinalSegment &) const;
    struct Stats { uint64_t pushes = 0, confirmedTokens = 0, regressions = 0, mismatchChars = 0, finalChars = 0; };
    Stats stats() const;
    // 由调用方在 final 到达、reset 前调用：把 mismatchAgainst 结果计入 stats（L-Q1 统计）。
    void recordFinal(const FinalSegment &);

    int n() const noexcept { return m_n; }
    int guardMs() const noexcept { return m_guardMs; }

private:
    int m_n;
    int m_guardMs;
    ConfirmedPrefix m_state;
    std::deque<std::vector<Word>> m_history;        // 最近 n 次规整后 token 序列（实现方可改）
    Stats m_stats;
};

} // namespace subtitle

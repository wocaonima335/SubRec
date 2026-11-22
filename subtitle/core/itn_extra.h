#pragma once
// M0 冻结：补充 ITN 规则（design-live-v2.md §10.3 FixerRules::itnExtra、§14.5、D33）。M3 功能，默认空。
// 接口归属：M0；实现归工作包 K。CMake 目标 subtitle_core。
//
// M1 只用 SenseVoice 自带 ITN；这里是 M3 的补充规则（如「百分之五十」→「50%」），规则为空时 applyItn 原样返回。
// 线程：纯函数，任意线程。

#include <string>
#include <string_view>
#include <vector>

namespace subtitle {

struct ItnRule {
    std::string name;          // 日志/测试用标识
    std::string pattern;       // 匹配模式（语法由 K 在 M3 定稿，M0 只占位）
    std::string replacement;
};

struct ItnRules {
    std::vector<ItnRule> rules;   // 空 = 不做补充 ITN
};

std::string applyItn(std::string_view text, const ItnRules &rules);

} // namespace subtitle

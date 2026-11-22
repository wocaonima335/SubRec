#pragma once
// M0 冻结：幻觉过滤（design-verified.md §11.3 B、§9.4；design-live-v2.md §4.3.2 第 2 步）。
// 接口归属：M0；实现归工作包 B。CMake 目标 subtitle_core（纯 C++17，纯函数）。
//
// 命中任一规则即判为幻觉：黑名单整句匹配、同一短语重复次数超限、字速异常（过快/过慢）、
// 段能量低于门限（由宿主在 Segment 中给出 noSpeechProb 时使用）。
// 线程：无共享状态，任意线程。

#include "subtitle/subtitle_types.h"

#include <string>
#include <vector>

namespace subtitle {

struct HallucinationRules {
    std::vector<std::string> blacklist;     // 整句（规整后）匹配即丢弃；初值由 B 定稿
    int maxRepeat = 4;                      // 同一短语连续重复次数上限
    double maxCharsPerSec = 12.0;           // 有效字符 / 秒 上限
    double minCharsPerSec = 0.3;            // 下限（长段只出一两个字）
    float maxNoSpeechProb = 0.8f;           // 引擎给出时生效；NAN 时跳过该规则
};

bool isLikelyHallucination(const Segment &, const HallucinationRules &);

} // namespace subtitle

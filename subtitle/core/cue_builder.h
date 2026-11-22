#pragma once
// M0 冻结：CueBuilder（design-live-v2.md §10.3 B、§8.2；上版 design-verified.md §11.3 B）。
// 接口归属：M0；实现归工作包 B。CMake 目标 subtitle_core（纯 C++17，纯函数）。
//
// buildCues：按标点 / 字数（maxCharsPerLine × maxLines，按 effectiveCharCount）/ 时长规则把一个 final
//   切成若干 cue；有 token 时间戳时用 token 时间，没有时按字数线性插值。
//   hardBoundariesMs = splice 点（成片毫秒，升序）：任何 cue 都不得跨越其中任一点（§8.2 最后一条）。
//   输出 cue 的 segmentId = final.id，provisional = false。
// resolveOverlaps：全局排序后保证单调不重叠（定稿前调用）。
// 线程：无共享状态，任意线程。

#include "subtitle/subtitle_types.h"

#include <cstdint>
#include <vector>

namespace subtitle {

std::vector<Cue> buildCues(const FinalSegment &, const CueConfig &,
                           const std::vector<int64_t> &hardBoundariesMs);   // splice 点

void resolveOverlaps(std::vector<Cue> &);

} // namespace subtitle

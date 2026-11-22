#pragma once
// M0 冻结：句末 2pass 第一层——句末规则修正 SentenceFixer（design-live-v2.md §4.3.2、§10.3 H、D27、D31）。
// 接口归属：M0；实现归工作包 H（规则扩充 glossary/eventTags/itnExtra 的函数由 K 提供）。
// CMake 目标 subtitle_live（纯 C++17）。
//
// apply 的处理顺序（§4.3.2）：
//   1. normalizeText（rules.normalize）；规整后为空 → Dropped。
//   2. isLikelyHallucination（rules.hallucination）→ Dropped；调用方据此删除同句临时 cue。
//   3. 跨句合并：doc.revisableTail(窗口) 的最后一条 cue 无句末标点、与本句间隔 < mergeGapMs、
//      合并后不超过 maxCharsPerLine × maxLines → 合并进上一条（时间取并集）→ MergedIntoPrevious。
//      录制中窗口 = LiveConfig::revisableCues（默认 2）；录完（recording=false）全篇可修订。
//   4. （M2）glossary 替换、eventTags；（M3）itnExtra。
//   返回 Kept 时 io 已被改写，调用方继续 CueBuilder → Document。
// 规则都是确定性纯函数，CI 做 golden 测试；耗时 < 1 ms/句。
// 线程：只在 SubtitleJobRunner 线程使用。

#include "subtitle/core/glossary.h"
#include "subtitle/core/hallucination_filter.h"
#include "subtitle/core/itn_extra.h"
#include "subtitle/core/text_normalizer.h"
#include "subtitle/subtitle_types.h"

#include <vector>

namespace subtitle {

class SubtitleDocument;

struct FixerRules {
    NormalizeOptions normalize;  HallucinationRules hallucination;
    bool mergeShortCues = true;  int mergeGapMs = 300;
    std::vector<GlossaryEntry> glossary;            // M2：热词词表（词形 + 拼音键 + 是否送 Whisper prompt）
    bool eventTags = false;                         // M2：把 FinalSegment.event 映射为 [音乐]/[掌声]/[笑声]
    ItnRules itnExtra;                              // M3：补充 ITN（默认空）
    CueConfig cue;                                  // 合并上限 maxCharsPerLine × maxLines 取自这里
    int revisableCues = 2;                          // 录制中的可修订窗口（= LiveConfig::revisableCues）
};

class SentenceFixer {
public:
    explicit SentenceFixer(FixerRules);
    enum class Outcome { Kept, Dropped, MergedIntoPrevious };
    // 在 final 写入 Document 前调用；可改写 doc.revisableTail() 内的 cue
    Outcome apply(FinalSegment &io, SubtitleDocument &doc, bool recording);
    const FixerRules &rules() const noexcept { return m_rules; }

private:
    FixerRules m_rules;
};

} // namespace subtitle

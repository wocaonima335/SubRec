// M0 桩：subtitle_core 中归工作包 K 的部分（glossary / event_tags / itn_extra，M2/M3）。
// K 实现时拆成 glossary.cpp / event_tags.cpp / itn_extra.cpp 并删除本文件。桩行为：不做任何改写。

#include "subtitle/core/event_tags.h"
#include "subtitle/core/glossary.h"
#include "subtitle/core/itn_extra.h"

namespace subtitle {

int applyGlossary(std::string &, const std::vector<GlossaryEntry> &) { return 0; }
bool glossaryMiss(std::string_view, const std::vector<GlossaryEntry> &) { return false; }
std::string buildWhisperPrompt(const std::vector<GlossaryEntry> &, int) { return {}; }

std::string eventTagFor(std::string_view) { return {}; }
bool applyEventTag(FinalSegment &) { return false; }

std::string applyItn(std::string_view text, const ItnRules &) { return std::string(text); }

} // namespace subtitle

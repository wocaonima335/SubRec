// M0 桩：subtitle_core 中归工作包 B 的部分（text_normalizer / hallucination_filter / cue_builder / subtitle_writers）。
// B 实现时按头文件拆成同名 .cpp 并删除本文件。桩行为：原样返回或返回空结果；写文件返回 NotSupported。

#include "subtitle/core/cue_builder.h"
#include "subtitle/core/hallucination_filter.h"
#include "subtitle/core/subtitle_writers.h"
#include "subtitle/core/text_normalizer.h"

namespace subtitle {

std::string normalizeText(std::string_view raw, const NormalizeOptions &) { return std::string(raw); }
std::size_t countCodePoints(std::string_view utf8) noexcept { return utf8.size(); }
std::size_t effectiveCharCount(std::string_view utf8) noexcept { return utf8.size(); }
double asciiLetterRatio(std::string_view) noexcept { return 0.0; }

bool isLikelyHallucination(const Segment &, const HallucinationRules &) { return false; }

std::vector<Cue> buildCues(const FinalSegment &, const CueConfig &, const std::vector<int64_t> &) { return {}; }
void resolveOverlaps(std::vector<Cue> &) {}

std::string formatSrtTimestamp(int64_t) { return "00:00:00,000"; }
std::string formatSrt(const std::vector<Cue> &, bool, bool) { return {}; }
std::string formatSrt(const std::vector<Cue> &cues, const SrtOptions &o) { return formatSrt(cues, o.bom, o.crlf); }
std::string formatVtt(const std::vector<Cue> &) { return {}; }
std::string formatAss(const std::vector<Cue> &, const AssStyle &) { return {}; }
std::string toJson(const std::vector<Segment> &) { return "[]"; }
bool fromJson(std::string_view, std::vector<Segment> &) { return false; }
bool parseSrt(std::string_view, std::vector<Cue> &) { return false; }

Status writeFileAtomic(const std::filesystem::path &, std::string_view)
{
    return Status::error(ErrorCode::NotSupported, "M0 stub: writeFileAtomic");
}

std::filesystem::path srtPathForMp4(const std::filesystem::path &mp4)
{
    std::filesystem::path p = mp4;
    return p.replace_extension(".srt");
}
std::filesystem::path srtPartPath(const std::filesystem::path &srt) { return std::filesystem::path(srt.native() + std::filesystem::path(".part").native()); }
std::filesystem::path liveSrtPath(const std::filesystem::path &srt)
{
    std::filesystem::path p = srt;
    return p.replace_extension(".live.srt");
}
std::filesystem::path refinePartPath(const std::filesystem::path &srt) { return std::filesystem::path(srt.native() + std::filesystem::path(".refine.part").native()); }

} // namespace subtitle

#include "subtitle/live/sentence_fixer.h"
#include "subtitle/live/subtitle_merger.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace subtitle {
namespace {
bool sentenceEnd(const std::string &text)
{
    const auto end = text.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) return false;
    auto begin = end;
    while (begin && (static_cast<unsigned char>(text[begin]) & 0xc0) == 0x80) --begin;
    return std::string("。！？；….!?;").find(text.substr(begin, end - begin + 1)) != std::string::npos;
}
}
SentenceFixer::SentenceFixer(FixerRules rules) : m_rules(std::move(rules)) {}
SentenceFixer::Outcome SentenceFixer::apply(FinalSegment &io, SubtitleDocument &doc, bool recording)
{
    io.text = normalizeText(io.text, m_rules.normalize);
    Segment segment;
    segment.id = io.id; segment.begin16k = io.begin16k; segment.end16k = io.end16k;
    segment.text = io.text; segment.words = io.words; segment.language = io.language;
    if (io.text.empty() || isLikelyHallucination(segment, m_rules.hallucination)) return Outcome::Dropped;
    if (!m_rules.mergeShortCues) return Outcome::Kept;
    auto tail = doc.revisableTail(recording ? std::max(0, m_rules.revisableCues) :
        int(std::min<size_t>(doc.cues().size(), std::numeric_limits<int>::max())));
    // Select the chronological predecessor. In finished mode it may be anywhere
    // in the document; an out-of-order final must never merge with a later cue.
    Cue *previous = nullptr;
    for (auto *cue : tail) {
        if (cue->segmentId != io.id && cue->endMs <= asrSamplesToMs(io.begin16k)) previous = cue;
    }
    if (!previous || previous->edited || doc.state(previous->segmentId) == SentenceState::UserEdited ||
        sentenceEnd(previous->text) || asrSamplesToMs(io.begin16k) - previous->endMs >= m_rules.mergeGapMs)
        return Outcome::Kept;
    for (const auto boundary : doc.hardBoundaries16k())
        if (boundary > previous->beginMs * kAsrSamplesPerMs && boundary < io.end16k) return Outcome::Kept;
    // Preserve Latin word separation, without inserting spaces between CJK tokens.
    const bool space = !previous->text.empty() && !io.text.empty() &&
        static_cast<unsigned char>(previous->text.back()) < 128 &&
        static_cast<unsigned char>(io.text.front()) < 128 && previous->text.back() != ' ' && io.text.front() != ' ';
    const auto merged = previous->text + (space ? " " : "") + io.text;
    const auto maxChars = size_t(std::max(1, m_rules.cue.maxCharsPerLine)) * size_t(std::max(1, m_rules.cue.maxLines));
    if (effectiveCharCount(merged) > maxChars) return Outcome::Kept;
    previous->text = merged;
    previous->endMs = std::max(previous->endMs, asrSamplesToMs(io.end16k));
    doc.m_mergedInto[io.id] = previous->segmentId;
    ++doc.m_revision; // Mutable cue pointers do not themselves notify the document.
    return Outcome::MergedIntoPrevious;
}
} // namespace subtitle

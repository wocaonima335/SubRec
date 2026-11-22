#include "subtitle/core/cue_builder.h"
#include "subtitle/core/text_normalizer.h"

#include <algorithm>
#include <limits>
#include <string_view>

namespace subtitle {
namespace {
struct Atom {
    std::string text;
    size_t first = 0, last = 0;
    int64_t begin = 0, end = 0;
    size_t weight = 0;
    bool sentence = false, comma = false;
};
bool contains(std::string_view set, std::string_view cp) { return set.find(cp) != std::string_view::npos; }
int64_t interpolate(int64_t begin, int64_t end, size_t i, size_t n)
{
    if (!n || i == n) return end;
    return begin + static_cast<int64_t>(static_cast<long double>(end - begin) * i / n);
}
std::string trimmed(std::string text)
{
    while (!text.empty() && text.back() == ' ') text.pop_back();
    const auto first = text.find_first_not_of(' ');
    return first == std::string::npos ? std::string{} : text.substr(first);
}
} // namespace

std::vector<Cue> buildCues(const FinalSegment &final, const CueConfig &config,
                           const std::vector<int64_t> &hardBoundariesMs)
{
    const int64_t begin = std::max<int64_t>(0, asrSamplesToMs(final.begin16k));
    const int64_t end = asrSamplesToMs(final.end16k);
    NormalizeOptions options;
    options.dropFillerOnly = false; // Filtering belongs to the caller.
    const auto text = normalizeText(final.text, options);
    if (text.empty() || end <= begin) return {};
    const size_t maxChars = size_t(std::max(1, config.maxCharsPerLine)) * size_t(std::max(1, config.maxLines));
    const int64_t maxDuration = std::max(1, config.maxDurationMs);
    const int64_t minDuration = std::clamp<int64_t>(config.minDurationMs, 0, maxDuration);
    std::vector<Atom> atoms;
    for (size_t i = 0; i < text.size();) {
        const size_t first = i++;
        while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80) ++i;
        const std::string cp = text.substr(first, i - first);
        const size_t weight = effectiveCharCount(cp);
        const bool punctuation = contains("。！？；….!?;", cp);
        const bool comma = contains("，、,:：", cp);
        // Latin/digit runs are indivisible. Punctuation/space stays with the preceding atom.
        if (!atoms.empty() && (!weight || !atoms.back().weight ||
            (atoms.back().weight && effectiveCharCount(atoms.back().text + cp) == 1))) {
            auto &a = atoms.back();
            a.text += cp;
            a.weight = effectiveCharCount(a.text);
            a.last = i;
            a.sentence = a.sentence || punctuation;
            a.comma = a.comma || comma;
        } else {
            atoms.push_back({cp, first, i, 0, 0, weight, punctuation, comma});
        }
    }
    size_t total = 0;
    for (const auto &a : atoms) total += a.weight;
    size_t used = 0;
    for (auto &a : atoms) {
        a.begin = interpolate(begin, end, used, total);
        used += a.weight;
        a.end = interpolate(begin, end, used, total);
    }

    // Align token text in order. If a model/rule changed the text, use the complete
    // interpolation fallback instead of mixing unrelated token and sentence times.
    struct TimedSpan { size_t first, last; int64_t begin, end; };
    std::vector<TimedSpan> spans;
    size_t cursor = 0;
    int64_t previous = begin;
    bool aligned = !final.words.empty();
    for (const auto &word : final.words) {
        const auto token = normalizeText(word.text, options);
        if (token.empty()) continue;
        const auto at = text.find(token, cursor);
        if (at == std::string::npos || effectiveCharCount(std::string_view(text).substr(cursor, at - cursor)) ||
            word.beginMs < previous || word.endMs < word.beginMs || word.beginMs < begin || word.endMs > end) {
            aligned = false; break;
        }
        spans.push_back({at, at + token.size(), word.beginMs, word.endMs});
        cursor = at + token.size();
        previous = word.endMs;
    }
    aligned = aligned && !spans.empty() && !effectiveCharCount(std::string_view(text).substr(cursor));
    if (aligned) {
        for (auto &a : atoms) {
            bool found = false;
            for (const auto &span : spans) if (a.first < span.last && a.last > span.first) {
                const auto full = std::string_view(text).substr(span.first, span.last - span.first);
                const size_t n = std::max<size_t>(1, effectiveCharCount(full));
                const size_t before = effectiveCharCount(std::string_view(text).substr(span.first,
                    std::max(a.first, span.first) - span.first));
                const size_t through = effectiveCharCount(std::string_view(text).substr(span.first,
                    std::min(a.last, span.last) - span.first));
                const auto b = interpolate(span.begin, span.end, before, n);
                const auto e = interpolate(span.begin, span.end, through, n);
                if (!found) a.begin = b;
                a.end = e;
                found = true;
            }
        }
    }

    std::vector<int64_t> boundaries{begin};
    for (auto b : hardBoundariesMs) if (b > begin && b < end) boundaries.push_back(b);
    boundaries.push_back(end);
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    // Assign indivisible words to the interval containing their midpoint. Clip at
    // splices rather than repeating the word on both sides of a hard boundary.
    std::vector<std::vector<Atom>> regions(boundaries.size() - 1);
    for (auto a : atoms) {
        const auto middle = a.begin + (a.end - a.begin) / 2;
        auto it = std::upper_bound(boundaries.begin(), boundaries.end(), middle);
        const size_t region = std::min<size_t>(regions.size() - 1, size_t(it - boundaries.begin() - 1));
        a.begin = std::clamp(a.begin, boundaries[region], boundaries[region + 1]);
        a.end = std::clamp(a.end, a.begin, boundaries[region + 1]);
        regions[region].push_back(std::move(a));
    }
    std::vector<Cue> result;
    for (size_t region = 0; region < regions.size(); ++region) {
        const auto &items = regions[region];
        const size_t regionStart = result.size();
        for (size_t first = 0; first < items.size();) {
            size_t last = first, count = 0, commaBreak = first;
            bool limitHit = false;
            while (last < items.size()) {
                const auto &a = items[last];
                if (last > first && (count + a.weight > maxChars || a.end - items[first].begin > maxDuration)) {
                    limitHit = true; break;
                }
                count += a.weight;
                ++last;
                if (a.comma) commaBreak = last;
                if (a.sentence) break;
            }
            if (limitHit && commaBreak > first) last = commaBreak;
            Cue cue;
            cue.beginMs = items[first].begin;
            cue.endMs = std::min(items[last - 1].end,
                cue.beginMs + std::min(maxDuration, boundaries[region + 1] - cue.beginMs));
            cue.segmentId = final.id;
            for (size_t i = first; i < last; ++i) cue.text += items[i].text;
            cue.text = trimmed(std::move(cue.text));
            if (!cue.text.empty()) result.push_back(std::move(cue));
            first = last;
        }
        for (size_t i = regionStart; i < result.size(); ++i) {
            auto &cue = result[i];
            const int64_t next = i + 1 < result.size() ? result[i + 1].beginMs : boundaries[region + 1];
            const auto cap = cue.beginMs + std::min(maxDuration, next - cue.beginMs);
            cue.endMs = std::min(cap, std::max(cue.endMs,
                cue.beginMs + std::min(minDuration, next - cue.beginMs)));
            if (next - cue.endMs < 100 && next <= cap) cue.endMs = next;
        }
    }
    resolveOverlaps(result);
    return result;
}

void resolveOverlaps(std::vector<Cue> &cues)
{
    for (auto &cue : cues) {
        cue.beginMs = std::max<int64_t>(0, cue.beginMs);
        cue.endMs = std::max(cue.beginMs, cue.endMs);
    }
    std::stable_sort(cues.begin(), cues.end(), [](const Cue &a, const Cue &b) {
        return a.beginMs < b.beginMs;
    });
    // Trim the preceding cue, preserving later start times (including splice points).
    for (size_t i = 1; i < cues.size(); ++i)
        cues[i - 1].endMs = std::min(cues[i - 1].endMs, cues[i].beginMs);
}
} // namespace subtitle

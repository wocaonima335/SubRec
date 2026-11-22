#include "subtitle/live/local_agreement.h"
#include "subtitle/core/text_normalizer.h"

#include <algorithm>
#include <numeric>

namespace subtitle {
namespace {
// B sanitizes malformed UTF-8. The sentinel prevents its sentence-level
// punctuation-only filter from discarding code points needed by agreement.
std::vector<std::string> points(const std::string &text)
{
    NormalizeOptions o; o.stripEngineTags = false; o.collapseWhitespace = false; o.dropFillerOnly = false;
    const auto clean = normalizeText("x" + text, o).substr(1);
    std::vector<std::string> result;
    for (size_t i = 0; i < clean.size();) {
        const auto b = i++;
        while (i < clean.size() && (static_cast<unsigned char>(clean[i]) & 0xc0) == 0x80) ++i;
        result.push_back(clean.substr(b, i - b));
    }
    return result;
}
std::string key(const std::string &s)
{
    std::string out;
    for (auto cp : points(s)) {
        uint32_t c = static_cast<unsigned char>(cp[0]);
        if (cp.size() > 1) {
            c &= (1u << (7 - cp.size())) - 1;
            for (size_t i = 1; i < cp.size(); ++i) c = (c << 6) | (static_cast<unsigned char>(cp[i]) & 63);
        }
        if (c >= 0xff01 && c <= 0xff5e) { c -= 0xfee0; cp = std::string(1, char(c)); }
        NormalizeOptions options; options.dropFillerOnly = false;
        if (normalizeText(cp, options).empty()) continue;
        if (c >= 'A' && c <= 'Z') cp = std::string(1, char(c + 32));
        out += cp;
    }
    return out;
}
struct Group { std::string normalized; size_t end; int64_t endMs; };
std::vector<Group> groups(const std::vector<Word> &words)
{
    std::vector<Group> out;
    for (size_t i = 0; i < words.size(); ++i) {
        auto k = key(words[i].text);
        if (!k.empty()) out.push_back({std::move(k), i + 1, words[i].endMs});
        else if (!out.empty()) out.back().end = i + 1; // punctuation follows its lexical token
    }
    return out;
}
}
LocalAgreement::LocalAgreement(int n, int guardMs)
    : m_n(std::clamp(n, 2, 3)), m_guardMs(std::max(0, guardMs)) {}
void LocalAgreement::reset(SegId id)
{
    m_state = {}; m_state.id = id; m_history.clear();
}
std::vector<Word> LocalAgreement::push(const PartialSegment &p)
{
    ++m_stats.pushes;
    if (p.id != m_state.id || (!m_history.empty() && p.job != m_state.job)) reset(p.id);
    m_state.job = p.job;
    if (m_state.frozen) return {};
    auto words = p.tokens;
    const bool fallback = words.empty();
    if (fallback) {
        for (const auto &cp : points(p.text)) words.push_back({asrSamplesToMs(p.begin16k), asrSamplesToMs(p.upto16k), cp});
    }
    m_history.push_back(words);
    if (m_history.size() > size_t(m_n)) m_history.pop_front();
    // In the compatibility path every code point (including punctuation) is a unit.
    auto units = [&](const std::vector<Word> &w) {
        if (!fallback) return groups(w);
        std::vector<Group> g;
        for (size_t i = 0; i < w.size(); ++i) g.push_back({w[i].text, i + 1, w[i].endMs});
        return g;
    };
    const auto latest = units(words), confirmed = units(m_state.confirmed);
    size_t length = 0;
    if (m_history.size() == size_t(m_n)) {
        length = latest.size();
        for (const auto &h : m_history) {
            const auto g = units(h);
            size_t i = 0;
            while (i < length && i < g.size() && g[i].normalized == latest[i].normalized) ++i;
            length = i;
        }
        // A revision that persists for n observations still cannot replace the frozen prefix.
        size_t match = 0;
        while (match < confirmed.size() && match < latest.size() &&
               confirmed[match].normalized == latest[match].normalized) ++match;
        if (length < confirmed.size() || match < confirmed.size()) {
            ++m_stats.regressions; length = 0;
        }
        if (!fallback) {
            size_t safe = 0;
            while (safe < length && latest[safe].endMs <= asrSamplesToMs(p.upto16k) - m_guardMs) ++safe;
            length = safe;
        }
    }
    std::vector<Word> added;
    if (length > confirmed.size()) {
        const size_t begin = confirmed.empty() ? 0 : latest[confirmed.size() - 1].end;
        added.assign(words.begin() + begin, words.begin() + latest[length - 1].end);
        m_state.confirmed.insert(m_state.confirmed.end(), added.begin(), added.end());
        for (const auto &w : added) m_state.confirmedText += w.text;
        m_state.confirmedUpto16k = std::max(m_state.confirmedUpto16k, added.back().endMs * kAsrSamplesPerMs);
        m_stats.confirmedTokens += added.size();
    }
    m_state.tentativeText.clear();
    const auto count = units(m_state.confirmed).size();
    const size_t offset = count && count <= latest.size() ? latest[count - 1].end : (count ? words.size() : 0);
    for (size_t i = offset; i < words.size(); ++i) m_state.tentativeText += words[i].text;
    return added;
}
void LocalAgreement::freeze() { m_state.frozen = true; m_state.tentativeText.clear(); }
const ConfirmedPrefix &LocalAgreement::state() const { return m_state; }
int LocalAgreement::mismatchAgainst(const FinalSegment &f) const
{
    const auto a = points(m_state.confirmedText); auto b = points(f.text);
    if (b.size() > a.size()) b.resize(a.size());
    std::vector<int> previous(b.size() + 1), row(b.size() + 1);
    std::iota(previous.begin(), previous.end(), 0);
    for (size_t i = 0; i < a.size(); ++i) {
        row[0] = int(i + 1);
        for (size_t j = 0; j < b.size(); ++j)
            row[j + 1] = std::min({previous[j + 1] + 1, row[j] + 1, previous[j] + (a[i] != b[j])});
        previous.swap(row);
    }
    return previous.back();
}
LocalAgreement::Stats LocalAgreement::stats() const { return m_stats; }
void LocalAgreement::recordFinal(const FinalSegment &f)
{
    m_stats.mismatchChars += mismatchAgainst(f);
    m_stats.finalChars += countCodePoints(f.text);
}
} // namespace subtitle

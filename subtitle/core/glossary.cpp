#include "subtitle/core/glossary.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace subtitle {
namespace {
#include "rules_utf8.inc"
#include "pinyin_table.inc"

// K / R30 matching policy (no model, locale, fuzzy edit distance or tokenizer):
// * Han terms: >= 2 table-supported characters, equal character count, exact
//   toneless syllable sequence, and at least one unchanged character as an anchor.
//   Explicit keys are space-separated ASCII syllables (v for ü), case-insensitive;
//   optional trailing tone digits 1..5 are ignored. They must agree with the table.
//   Unknown characters, mixed scripts, single Han characters and invalid UTF-8
//   fail closed. No punctuation/space skipping or partial-character matching.
// * Protect characters in every exact glossary term and the common words below:
//   overlapping candidates may keep those characters but never change them.
//   This small list is NOT a dictionary or
//   a proof that an unlisted homophone is incorrect; PC3 review remains needed.
// * ASCII entries: [A-Za-z][A-Za-z0-9_]* only, case-insensitive whole words;
//   adjacent non-ASCII characters also block matching (avoid accented words).
//   Their pinyinKey is ignored; ASCII never participates in pinyin matching.
// * Scan the ORIGINAL text left-to-right; longest candidate wins, equal-length
//   different targets are ambiguous and skipped. Never overlap or cascade.
// glossaryMiss uses the same safeguards, but an entry already present exactly
// anywhere is not missing, even if a different occurrence is misspelled.
constexpr std::string_view commonWords[] = {
    "我们", "你们", "他们", "这个", "那个", "时间", "世界", "事实", "实事",
    "方式", "方法", "事情", "开始", "结束", "现在", "已经", "可以", "知道",
    "因为", "所以", "中国", "北京", "上海", "公司", "工作", "公共", "工具",
    "科技", "科学", "技术", "数据", "程序", "软件", "硬件", "视频", "音频",
    "字幕", "文字", "输入", "输出", "文件", "会议", "学习", "学校", "学生",
    "老师", "城市", "市场", "实验", "试验", "权利", "权力", "需要", "必须",
    "手机", "电脑", "音乐", "掌声", "笑声", "人工", "智能", "苹果"
};

char lowerAscii(char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; }
bool letter(char32_t c) { return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z'); }
bool wordChar(char32_t c) { return letter(c) || (c >= U'0' && c <= U'9') || c == U'_'; }
bool blocksWord(char32_t c) { return c >= 0x80 || wordChar(c); }

std::string_view pinyin(char32_t cp)
{
    for (const auto &group : pinyinTable)
        if (group.characters.find(cp) != std::u32string_view::npos) return group.syllable;
    return {};
}

struct Entry {
    const GlossaryEntry *source;
    std::vector<Rune> characters;
    std::vector<std::string_view> syllables;
    bool ascii = false;
    bool present = false;
};

bool keyAgrees(std::string_view key, const std::vector<std::string_view> &syllables)
{
    if (key.empty()) return true;
    size_t pos = 0, index = 0;
    while (pos < key.size()) {
        if (key[pos] == ' ') { ++pos; continue; }
        std::string token;
        while (pos < key.size() && key[pos] != ' ') token += lowerAscii(key[pos++]);
        if (!token.empty() && token.back() >= '1' && token.back() <= '5') token.pop_back();
        if (index == syllables.size() || token != syllables[index++]) return false;
    }
    return index == syllables.size();
}

std::vector<Entry> prepare(const std::vector<GlossaryEntry> &glossary)
{
    std::vector<Entry> entries;
    for (const auto &source : glossary) {
        Entry e{&source, {}, {}, false, false};
        if (!decodeUtf8(source.term, e.characters) || e.characters.empty()) continue;
        e.ascii = letter(e.characters.front().value) &&
            std::all_of(e.characters.begin(), e.characters.end(),
                        [](const Rune &r) { return wordChar(r.value); });
        if (!e.ascii) {
            if (e.characters.size() < 2) continue;
            for (const auto &r : e.characters) {
                const auto syllable = pinyin(r.value);
                if (syllable.empty()) break;
                e.syllables.push_back(syllable);
            }
            if (e.syllables.size() != e.characters.size() ||
                !keyAgrees(source.pinyinKey, e.syllables)) continue;
        }
        entries.push_back(std::move(e));
    }
    return entries;
}

bool wholeWord(const std::vector<Rune> &runes, size_t i, size_t n)
{
    return (i == 0 || !blocksWord(runes[i - 1].value)) &&
           (i + n == runes.size() || !blocksWord(runes[i + n].value));
}

struct Match { size_t begin, end; const GlossaryEntry *entry; };

std::vector<Match> findMatches(std::string_view text,
                             const std::vector<GlossaryEntry> &glossary, bool missingOnly)
{
    std::vector<Rune> runes;
    if (glossary.empty() || !decodeUtf8(text, runes)) return {};
    auto entries = prepare(glossary);
    std::vector<bool> protectedBytes(text.size(), false);
    // Valid UTF-8 exact strings cannot start/end inside another UTF-8 character.
    const auto protect = [&](std::string_view term, bool ascii) {
        bool present = false;
        for (size_t pos = 0; !term.empty() && (pos = text.find(term, pos)) != text.npos; ++pos) {
            if (ascii) {
                const auto it = std::lower_bound(runes.begin(), runes.end(), pos,
                    [](const Rune &r, size_t p) { return r.begin < p; });
                if (it == runes.end() || !wholeWord(runes, size_t(it - runes.begin()), term.size())) continue;
            }
            present = true;
            std::fill(protectedBytes.begin() + pos, protectedBytes.begin() + pos + term.size(), true);
        }
        return present;
    };
    // Protect even unsupported/mixed-script exact glossary terms.
    for (const auto &entry : glossary) {
        std::vector<Rune> chars;
        if (!decodeUtf8(entry.term, chars)) continue;
        const bool ascii = !chars.empty() && letter(chars.front().value) &&
            std::all_of(chars.begin(), chars.end(), [](const Rune &r) { return wordChar(r.value); });
        protect(entry.term, ascii);
    }
    for (auto &e : entries) e.present = protect(e.source->term, e.ascii);
    for (const auto term : commonWords) protect(term, false);

    std::vector<Match> matches;
    for (size_t i = 0; i < runes.size();) {
        const Entry *best = nullptr;
        size_t longest = 0;
        bool ambiguous = false;
        for (const auto &e : entries) {
            const size_t n = e.characters.size();
            if ((missingOnly && e.present) || n > runes.size() - i) continue;
            const size_t begin = runes[i].begin, end = runes[i + n - 1].end;
            bool match = true, anchor = false;
            if (e.ascii && !wholeWord(runes, i, n)) continue;
            for (size_t j = 0; j < n; ++j) {
                const char32_t cp = runes[i + j].value;
                anchor = anchor || cp == e.characters[j].value;
                if (protectedBytes[runes[i + j].begin] && cp != e.characters[j].value) {
                    match = false; break;
                }
                if (e.ascii ? (cp >= 0x80 || lowerAscii(char(cp)) != lowerAscii(char(e.characters[j].value))) :
                    (pinyin(cp) != e.syllables[j])) { match = false; break; }
            }
            if (!match || (!e.ascii && !anchor) || text.substr(begin, end - begin) == e.source->term) continue;
            if (n > longest) { best = &e; longest = n; ambiguous = false; }
            else if (n == longest && best->source->term != e.source->term) ambiguous = true;
        }
        if (best && !ambiguous) {
            matches.push_back({runes[i].begin, runes[i + longest - 1].end, best->source});
            i += longest;
        } else {
            // Do not fall back to a shorter/overlapping match inside an ambiguity.
            i += ambiguous ? longest : 1;
        }
    }
    return matches;
}
} // namespace

int applyGlossary(std::string &text, const std::vector<GlossaryEntry> &glossary)
{
    const auto matches = findMatches(text, glossary, false);
    if (matches.empty()) return 0;
    std::string result;
    size_t copied = 0;
    int count = 0;
    for (const auto &m : matches) {
        // The public count is int; stop applying if it cannot represent more.
        if (count == std::numeric_limits<int>::max()) break;
        result.append(text, copied, m.begin - copied);
        result += m.entry->term;
        copied = m.end;
        ++count;
    }
    result.append(text, copied, text.size() - copied);
    text = std::move(result);
    return count;
}

bool glossaryMiss(std::string_view text, const std::vector<GlossaryEntry> &glossary)
{
    return !findMatches(text, glossary, true).empty();
}

std::string buildWhisperPrompt(const std::vector<GlossaryEntry> &glossary, int maxTokens)
{
    // Deterministic conservative estimate: ONE token per UTF-8 BYTE, including
    // the ", " separators. This is not Whisper tokenization. Keep vector/MRU
    // order and stop at the first eligible entry that does not fit; no skipping
    // oversized entries to admit older ones. Empty/invalid UTF-8 entries are
    // ignored. Append whole entries only, hence never split a UTF-8 character.
    if (maxTokens <= 0) return {};
    const auto budget = static_cast<size_t>(maxTokens);
    std::string prompt;
    std::vector<Rune> runes;
    for (const auto &e : glossary) {
        if (!e.sendToPrompt || e.term.empty() || !decodeUtf8(e.term, runes)) continue;
        const size_t separator = prompt.empty() ? 0 : 2;
        const size_t remaining = budget - prompt.size();
        if (separator > remaining || e.term.size() > remaining - separator) break;
        if (separator) prompt += ", ";
        prompt += e.term;
    }
    return prompt;
}
} // namespace subtitle

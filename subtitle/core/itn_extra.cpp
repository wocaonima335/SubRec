#include "subtitle/core/itn_extra.h"

namespace subtitle {
namespace {
#include "rules_utf8.inc"
}

std::string applyItn(std::string_view text, const ItnRules &rules)
{
    // K / M3 minimal syntax: case-sensitive, literal UTF-8 substrings. No regex,
    // captures, escapes or special replacement syntax; name is metadata only.
    // Rules run in vector order; each replaces non-overlapping occurrences from
    // left to right. A rule never reprocesses its own inserted text, but later
    // rules see earlier output. Empty patterns and invalid UTF-8 rules are ignored;
    // empty replacements delete. Invalid UTF-8 input is preserved byte-for-byte.
    // With no rules, ALL input bytes are returned untouched (including NULs).
    std::string result(text);
    if (rules.rules.empty()) return result;
    std::vector<Rune> runes;
    if (!decodeUtf8(text, runes)) return result;
    for (const auto &rule : rules.rules) {
        if (rule.pattern.empty() || !decodeUtf8(rule.pattern, runes) ||
            !decodeUtf8(rule.replacement, runes)) continue;
        size_t pos = 0;
        while ((pos = result.find(rule.pattern, pos)) != result.npos) {
            result.replace(pos, rule.pattern.size(), rule.replacement);
            pos += rule.replacement.size();
        }
    }
    return result;
}
} // namespace subtitle

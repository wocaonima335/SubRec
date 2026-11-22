#include "subtitle/core/hallucination_filter.h"
#include "subtitle/core/text_normalizer.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace subtitle {
bool isLikelyHallucination(const Segment &segment, const HallucinationRules &rules)
{
    const auto text = normalizeText(segment.text, NormalizeOptions{});
    if (text.empty()) return true;
    // An empty configured list selects conservative built-ins; a supplied list replaces it.
    static const std::vector<std::string> defaults = {
        "谢谢观看", "谢谢观看。", "请订阅", "请不吝点赞 订阅 转发 打赏", "字幕由 Amara.org 社区提供"
    };
    for (const auto &entry : rules.blacklist.empty() ? defaults : rules.blacklist)
        if (text == normalizeText(entry, NormalizeOptions{})) return true;
    if (std::isfinite(segment.noSpeechProb) && segment.noSpeechProb > rules.maxNoSpeechProb) return true;
    if (segment.end16k <= segment.begin16k) return true;
    const double seconds = (double(segment.end16k) - double(segment.begin16k)) / kAsrSampleRate;
    const double rate = double(effectiveCharCount(text)) / seconds;
    if (rate > rules.maxCharsPerSec || rate < rules.minCharsPerSec) return true;

    // Remove punctuation/space before comparing code-point-aligned phrases:
    // a final repetition without its trailing separator still counts.
    std::string repeatedText;
    NormalizeOptions options;
    options.dropFillerOnly = false;
    for (size_t i = 0; i < text.size();) {
        const size_t start = i++;
        while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80) ++i;
        repeatedText += normalizeText(std::string_view(text).substr(start, i - start), options);
    }
    std::vector<size_t> offsets;
    for (size_t i = 0; i < repeatedText.size(); ++i)
        if ((static_cast<unsigned char>(repeatedText[i]) & 0xc0) != 0x80) offsets.push_back(i);
    offsets.push_back(repeatedText.size());
    const size_t limit = size_t(std::max(1, rules.maxRepeat));
    const size_t n = offsets.size() - 1;
    for (size_t width = 1; width <= n / (limit + 1); ++width) {
        size_t run = 1;
        for (size_t residue = 0; residue < width; ++residue) {
            run = 1;
            for (size_t i = residue + width; i + width <= n; i += width) {
                const std::string_view a(repeatedText.data() + offsets[i - width], offsets[i] - offsets[i - width]);
                const std::string_view b(repeatedText.data() + offsets[i], offsets[i + width] - offsets[i]);
                run = a == b ? run + 1 : 1;
                if (run > limit && effectiveCharCount(b) != 0) return true;
            }
        }
    }
    return false;
}
} // namespace subtitle

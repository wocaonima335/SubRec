#include "subtitle/core/event_tags.h"

namespace subtitle {
namespace {
struct EventTag { std::string_view event, tag; };
// Only the three categories specified by design §14.5 / D34 are supported.
// Event matching is ASCII case-insensitive, with no trimming or guessed aliases.
constexpr EventTag tags[] = {{"bgm", "[音乐]"}, {"applause", "[掌声]"}, {"laughter", "[笑声]"}};
bool equalEvent(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const char c = a[i] >= 'A' && a[i] <= 'Z' ? char(a[i] + ('a' - 'A')) : a[i];
        if (c != b[i]) return false;
    }
    return true;
}
} // namespace

std::string eventTagFor(std::string_view event)
{
    for (const auto &t : tags) if (equalEvent(event, t.event)) return std::string(t.tag);
    return {};
}

bool applyEventTag(FinalSegment &io)
{
    const auto tag = eventTagFor(io.event);
    if (tag.empty()) return false;
    // Any supported tag at byte zero counts as already tagged (even a different
    // category, a bare tag, or one without a following space). Preserve it exactly.
    // Tags later in the sentence and arbitrary bracketed text are not prefixes.
    for (const auto &t : tags) if (io.text.compare(0, t.tag.size(), t.tag) == 0) return false;
    io.text = tag + " " + io.text; // Empty text still uses the frozen "[标签] " format.
    return true;
}
} // namespace subtitle

#include "subtitle/core/event_tags.h"
#include "subtitle/core/glossary.h"
#include "subtitle/core/itn_extra.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#define CHECK(expr) do { if (!(expr)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #expr); } while (false)

using namespace subtitle;
namespace {
void unchanged(std::string text, const std::vector<GlossaryEntry> &entries)
{
    const auto original = text;
    CHECK(applyGlossary(text, entries) == 0);
    CHECK(text == original);
}

void glossary_pinyin_hits()
{
    struct Case { std::string text, term, key, expected; int count; };
    const Case cases[] = {
        {"请用科答讯飞。", "科大讯飞", "", "请用科大讯飞。", 1},
        {"科大训飞、科答讯飞", "科大讯飞", "ke da xun fei", "科大讯飞、科大讯飞", 2},
        {"科答讯飞", "科大讯飞", " KE1 DA4 XUN4 FEI1 ", "科大讯飞", 1},
        {"人公智能生成字慕", "人工智能", "", "人工智能生成字慕", 1},
        {"字慕与字慕", "字幕", "", "字幕与字幕", 2},
        {"白度", "百度", "", "百度", 1},
        {"😀科答讯飞𠀀", "科大讯飞", "", "😀科大讯飞𠀀", 1}
    };
    for (const auto &c : cases) {
        auto text = c.text;
        CHECK(applyGlossary(text, {{c.term, c.key, false}}) == c.count);
        CHECK(text == c.expected); // sendToPrompt does not gate corrections.
        CHECK(applyGlossary(text, {{c.term, c.key, false}}) == 0);
    }
}

void glossary_no_false_replacements()
{
    const std::vector<GlossaryEntry> glossary{{"科大讯飞", "", true}};
    for (const auto *text : {"", "科大讯飞", "科小讯飞", "科大飞讯", "科答 讯飞",
                             "科答，讯飞", "ke da xun fei", "科答讯", "科龘讯飞"}) {
        unchanged(text, glossary);
        CHECK(!glossaryMiss(text, glossary));
    }
    unchanged("科答讯飞", {});
    unchanged("科答讯飞", {{"科大讯飞", "ke xiao xun fei", true}});
    unchanged("科答讯飞", {{"科大讯飞", "kedaxunfei", true}});
    unchanged("科答讯飞", {{"科大讯飞", "ke da xun", true}});
    unchanged("科答讯飞", {{"科大讯飞", "ke da xun fei extra", true}});
    unchanged("科答讯飞", {{"科大讯飞", "ke da xun fei6", true}});
    unchanged("字", {{"自", "", true}}); // No single-character substitutions.
    unchanged("仁公", {{"人工", "", true}}); // No unchanged-character anchor.
    unchanged("科大AI", {{"科答AI", "", true}}); // Mixed script.
    unchanged("龘字", {{"达字", "da zi", true}}); // Unknown source character.
    unchanged("达字", {{"龘字", "da zi", true}}); // Unknown target, even with key.
    unchanged("重新", {{"重新", "chong xin", true}}); // Omitted polyphone.
    for (const auto &pair : std::vector<std::pair<std::string, std::string>>{
             {"权利", "权力"}, {"实验", "试验"}, {"公司", "公思"}, {"时间", "时见"}}) {
        unchanged(pair.first, {{pair.second, "", true}});
        CHECK(!glossaryMiss(pair.first, {{pair.second, "", true}}));
    }
    // A candidate must not rewrite part of a protected common word.
    unchanged("公司机", {{"思机", "", true}});
    const std::string malformed = std::string("\xff") + "科答讯飞";
    unchanged(malformed, glossary);
    CHECK(!glossaryMiss(malformed, glossary));
    unchanged("科答讯飞", {{"", "", true}, {"\xff", "", true}});
}

void glossary_conflicts_and_overlaps()
{
    std::string text = "科答讯飞";
    // Longest wins even when the shorter entry is earlier; no cascading.
    CHECK(applyGlossary(text, {{"科大", "", true}, {"科大讯飞", "", true},
                               {"科大训飞", "", true}}) == 0); // Ambiguous long targets.
    CHECK(text == "科答讯飞");
    CHECK(!glossaryMiss(text, {{"科大讯飞", "", true}, {"科大训飞", "", true}}));
    CHECK(applyGlossary(text, {{"科大", "", true}, {"科大讯飞", "", true}}) == 1);
    CHECK(text == "科大讯飞");
    text = "科答讯飞";
    CHECK(applyGlossary(text, {{"科大讯飞", "", true}, {"科大讯飞", "", false}}) == 1);
    unchanged("科大讯飞", {{"科大训飞", "", true}, {"科大讯飞", "", true}});
    unchanged("科答讯飞", {{"科答讯飞", "", true}, {"科大", "", true}});
    // Unsupported exact terms also protect their constituent characters.
    unchanged("科答AI", {{"科答AI", "", true}, {"科大", "", true}});
    text = "字慕字慕";
    CHECK(applyGlossary(text, {{"字幕", "", true}}) == 2);
    CHECK(text == "字幕字幕");
    text = "科答讯飞";
    CHECK(applyGlossary(text, {{"科大", "", true}, {"大训", "", true}}) == 1);
    CHECK(text == "科大讯飞"); // Second match cannot overlap/cascade on 大.
}

void glossary_ascii_words()
{
    const std::vector<GlossaryEntry> glossary{{"SubRec", "", true}};
    std::string text = "subrec, SUBREC (SubRec)";
    CHECK(applyGlossary(text, glossary) == 2);
    CHECK(text == "SubRec, SubRec (SubRec)");
    for (const auto *s : {"SubRec", "asubrec", "subrec2", "subrec_x", "x_subrec",
                          "ésubrec", "subrecé", "中subrec", "subrec中", "subre"}) unchanged(s, glossary);
    unchanged("subrec", {{"科大", "sub rec", true}});
    unchanged("C++", {{"c++", "", true}}); // Unsupported ASCII phrase/symbol syntax.
}

void glossary_miss()
{
    const std::vector<GlossaryEntry> glossary{{"科大讯飞", "", false}, {"字幕", "", true}};
    CHECK(glossaryMiss("科答讯飞", glossary));
    CHECK(glossaryMiss("字慕", glossary));
    CHECK(!glossaryMiss("科大讯飞", glossary));
    CHECK(!glossaryMiss("科大讯飞，科答讯飞", glossary)); // Exact term already present.
    CHECK(glossaryMiss("科大讯飞，字慕", glossary)); // Another entry is missing.
    CHECK(!glossaryMiss("没有相关内容", glossary));
    CHECK(!glossaryMiss("", glossary));
    CHECK(!glossaryMiss("科答讯飞", {}));
    CHECK(glossaryMiss("subrec", {{"SubRec", "", true}}));
    CHECK(!glossaryMiss("SubRec subrec", {{"SubRec", "", true}}));
    CHECK(!glossaryMiss("asubrec", {{"SubRec", "", true}}));
}

void prompt_filter_order()
{
    const std::vector<GlossaryEntry> entries{{"recent", "", true}, {"hidden", "", false},
        {"字幕", "", true}, {"older", "", true}, {"", "", true}};
    CHECK(buildWhisperPrompt(entries, 100) == "recent, 字幕, older");
    CHECK(buildWhisperPrompt({{"hidden", "", false}}, 100).empty());
    CHECK(buildWhisperPrompt({}, 100).empty());
    CHECK(buildWhisperPrompt({{"same", "", true}, {"same", "", true}}, 100) == "same, same");
}

void prompt_budget_boundaries()
{
    const std::vector<GlossaryEntry> entries{{"A", "", true}, {"字幕", "", true}, {"B", "", true}};
    CHECK(buildWhisperPrompt(entries, -1).empty());
    CHECK(buildWhisperPrompt(entries, 0).empty());
    CHECK(buildWhisperPrompt(entries, 1) == "A");
    CHECK(buildWhisperPrompt(entries, 8) == "A");
    CHECK(buildWhisperPrompt(entries, 9) == "A, 字幕");
    CHECK(buildWhisperPrompt(entries, 11) == "A, 字幕");
    CHECK(buildWhisperPrompt(entries, 12) == "A, 字幕, B");
    CHECK(buildWhisperPrompt({{"oversized", "", true}, {"a", "", true}}, 2).empty());
    CHECK(buildWhisperPrompt({{"oversized", "", false}, {"a", "", true}}, 2) == "a");
    CHECK(buildWhisperPrompt({{std::string(224, 'x'), "", true}}).size() == 224);
    CHECK(buildWhisperPrompt({{std::string(225, 'x'), "", true}}).empty());
    for (int budget = 0; budget <= 20; ++budget)
        CHECK(buildWhisperPrompt(entries, budget).size() <= static_cast<size_t>(budget));
}

void prompt_utf8_safety()
{
    for (const auto &term : {std::string("字幕"), std::string("😀"), std::string("é"), std::string("𠀀")}) {
        const std::vector<GlossaryEntry> entries{{term, "", true}};
        for (size_t budget = 0; budget < term.size(); ++budget)
            CHECK(buildWhisperPrompt(entries, static_cast<int>(budget)).empty());
        CHECK(buildWhisperPrompt(entries, static_cast<int>(term.size())) == term);
    }
    // Invalid entries are ignored, not repaired or emitted; strict UTF-8 cases.
    for (const auto *bad : {"\xff", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe4\xb8"})
        CHECK(buildWhisperPrompt({{bad, "", true}, {"ok", "", true}}, 100) == "ok");
}

void event_mapping()
{
    for (const auto &p : std::vector<std::pair<std::string, std::string>>{
        {"BGM", "[音乐]"}, {"bgm", "[音乐]"}, {"BgM", "[音乐]"},
        {"Applause", "[掌声]"}, {"APPLAUSE", "[掌声]"},
        {"Laughter", "[笑声]"}, {"lAuGhTeR", "[笑声]"}}) CHECK(eventTagFor(p.first) == p.second);
    for (const auto *s : {"", "Speech", "sPeEcH", "Unknown", "Music", "Cry", " BGM", "BGM ", "BGMx"})
        CHECK(eventTagFor(s).empty());
}

void event_application()
{
    for (const auto *event : {"", "Speech", "speech", "unknown"}) {
        FinalSegment s; s.event = event; s.text = "原始 text\n";
        CHECK(!applyEventTag(s));
        CHECK(s.text == "原始 text\n");
        s.text.clear(); CHECK(!applyEventTag(s)); CHECK(s.text.empty());
    }
    FinalSegment s; s.event = "Applause"; s.text = "你好";
    s.id = 42; s.begin16k = 160; s.end16k = 320; s.pass = 2;
    CHECK(applyEventTag(s)); CHECK(s.text == "[掌声] 你好");
    CHECK(s.event == "Applause" && s.id == 42 && s.begin16k == 160 && s.end16k == 320 && s.pass == 2);
    s.text.clear(); s.event = "BGM";
    CHECK(applyEventTag(s)); CHECK(s.text == "[音乐] ");
    s.text = "[speaker] Hello"; s.event = "Laughter";
    CHECK(applyEventTag(s)); CHECK(s.text == "[笑声] [speaker] Hello");
}

void event_idempotence()
{
    for (const auto *prefix : {"[音乐] ", "[掌声]", "[笑声]你好"}) {
        FinalSegment s; s.event = "BGM"; s.text = prefix;
        CHECK(!applyEventTag(s)); CHECK(s.text == prefix);
    }
    FinalSegment s; s.event = "Laughter"; s.text = "笑话";
    CHECK(applyEventTag(s)); const auto tagged = s.text;
    CHECK(!applyEventTag(s)); CHECK(s.text == tagged);
    s.text = "提到[笑声]";
    CHECK(applyEventTag(s)); CHECK(s.text == "[笑声] 提到[笑声]");
}

void itn_empty_identity()
{
    for (const auto &s : {std::string(), std::string("百分之五十 😀\r\n"),
                         std::string("  untouched  "), std::string("a\0b", 3), std::string("\xff\xc0\xaf")})
        CHECK(applyItn(s, {}) == s);
    CHECK(applyItn(std::string_view{}, {}).empty());
}

void itn_literal_order()
{
    const ItnRules rules{{{"percent", "百分之五十", "50%"}, {"followup", "50%", "一半"}}};
    CHECK(applyItn("百分之五十、百分之五十", rules) == "一半、一半");
    CHECK(applyItn("百分之五十", {{{"first", "50%", "一半"}, {"second", "百分之五十", "50%"}}}) == "50%");
    CHECK(applyItn("a.b axb", {{{"literal", "a.b", "$1\\x"}}}) == "$1\\x axb");
    CHECK(applyItn("abc ABC", {{{"case", "abc", "X"}}}) == "X ABC");
    CHECK(applyItn("a", {{{"no recursion", "a", "aa"}}}) == "aa");
    CHECK(applyItn("aaa", {{{"no overlap", "aa", "b"}}}) == "ba");
}

void itn_edge_cases()
{
    CHECK(applyItn("字字幕", {{{"delete", "字", ""}}}) == "幕");
    CHECK(applyItn("text", {{{"empty pattern", "", "insert"}}}) == "text");
    CHECK(applyItn("", {{{"nonempty", "a", "b"}}}).empty());
    CHECK(applyItn("字", {{{"partial byte", "\xad", "x"}}}) == "字");
    CHECK(applyItn("字", {{{"bad replacement", "字", "\xff"}}}) == "字");
    CHECK(applyItn("\xff字", {{{"valid rule", "字", "x"}}}) == "\xff字");
    CHECK(applyItn(std::string("a\0a", 3), {{{"nul", std::string(1, '\0'), "!"}}}) == "a!a");
}
} // namespace

int main(int argc, char **argv)
{
    const std::pair<const char *, void (*)()> cases[] = {
        {"glossary_pinyin_hits", glossary_pinyin_hits},
        {"glossary_no_false_replacements", glossary_no_false_replacements},
        {"glossary_conflicts_and_overlaps", glossary_conflicts_and_overlaps},
        {"glossary_ascii_words", glossary_ascii_words}, {"glossary_miss", glossary_miss},
        {"prompt_filter_order", prompt_filter_order}, {"prompt_budget_boundaries", prompt_budget_boundaries},
        {"prompt_utf8_safety", prompt_utf8_safety}, {"event_mapping", event_mapping},
        {"event_application", event_application}, {"event_idempotence", event_idempotence},
        {"itn_empty_identity", itn_empty_identity}, {"itn_literal_order", itn_literal_order},
        {"itn_edge_cases", itn_edge_cases}
    };
    if (argc != 2) return 2;
    for (const auto &test : cases) if (test.first == std::string(argv[1])) {
        try { test.second(); return 0; }
        catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    }
    std::cerr << "Unknown test: " << argv[1] << '\n';
    return 2;
}

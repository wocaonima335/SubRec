#include "test_support.h"
#include "subtitle/core/text_normalizer.h"
#include "subtitle/core/hallucination_filter.h"
#include "subtitle/core/cue_builder.h"
#include "subtitle/core/subtitle_writers.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
using namespace subtitle;

std::vector<Cue> goldenCues()
{
    return {{0, 1234, "你好，SubRec!\n第二行", 4}, {2000, 3000, "", 5},
        {360000001, 360062345, "100 小时 <ok> & 再见", 6, false, true}};
}
void golden_srt()
{
    auto cues = goldenCues();
    const auto golden = read(std::filesystem::path(SUBREC_CORE_GOLDEN_DIR) / "captions.srt");
    CHECK(formatSrt(cues, true, true) == golden);
    CHECK(formatSrt(cues, SrtOptions{}) == golden);
    CHECK(formatSrt(cues, false, true) == golden.substr(3));
    const auto lf = formatSrt(cues, false, false);
    CHECK(lf.find('\r') == std::string::npos && lf.find("\n2\n") != std::string::npos);
    CHECK(formatSrt({}, true, true) == "\xef\xbb\xbf");
    CHECK(formatSrtTimestamp(-1) == "00:00:00,000");
    CHECK(formatSrtTimestamp(360000000) == "100:00:00,000");
    cues = {{12, 45, "a\rb\r\nc\nd"}};
    CHECK(formatSrt(cues, false, true).find("a\r\nb\r\nc\r\nd") != std::string::npos);
}
void golden_vtt()
{
    CHECK(formatVtt(goldenCues()) == read(std::filesystem::path(SUBREC_CORE_GOLDEN_DIR) / "captions.vtt"));
    CHECK(formatVtt({}) == "WEBVTT\r\n\r\n");
}
void code_points()
{
    CHECK(countCodePoints("") == 0);
    CHECK(countCodePoints("A中😀𠀀é") == 6);
    CHECK(countCodePoints(std::string("\xc0\xaf\xed\xa0\x80\xf4\x90\x80\x80", 9)) == 9);
    CHECK(countCodePoints(std::string("a\0b", 3)) == 3);
    CHECK(countCodePoints("\xe4\xb8") == 2);
}
void effective_characters()
{
    CHECK(effectiveCharCount("你好，hello123 world 42！かなカナ 한글 𠀀") == 12);
    CHECK(effectiveCharCount("café naïve ＡＢ１２") == 3);
    CHECK(effectiveCharCount("，。！？… \t😀") == 0);
    CHECK(effectiveCharCount("中a1文") == 3);
    CHECK(effectiveCharCount("one-two") == 2);
    CHECK(effectiveCharCount("naïve ١٢٣") == 2);
}
void ascii_ratio()
{
    CHECK(asciiLetterRatio("， 。\t") == 0);
    CHECK(asciiLetterRatio("Hello, world!") == 1);
    CHECK(std::abs(asciiLetterRatio("中AB12😀") - 2. / 6) < 1e-10);
    CHECK(asciiLetterRatio("你好") == 0);
}
void normalization_tags()
{
    CHECK(normalizeText(" <|zh|><|NEUTRAL|>你好<|Speech|>world \t\n 再见　", {}) == "你好world 再见");
    CHECK(normalizeText("<|zh|><|Speech|>", {}).empty());
    CHECK(normalizeText("，。！？ \t", {}).empty());
    CHECK(normalizeText("،؛؟।", {}).empty());
    CHECK(normalizeText("hello <|broken", {}) == "hello <|broken");
    NormalizeOptions o; o.stripEngineTags = false;
    CHECK(normalizeText("<|zh|>你好", o) == "<|zh|>你好");
    o.collapseWhitespace = false;
    CHECK(normalizeText("  a\t b  ", o) == "  a\t b  ");
}
void normalization_fillers()
{
    CHECK(normalizeText("嗯，啊 呃哦唔额噢！", {}).empty());
    CHECK(normalizeText("嗯，今天很好啊。", {}) == "嗯，今天很好啊。");
    NormalizeOptions o; o.dropFillerOnly = false;
    CHECK(normalizeText("嗯啊", o) == "嗯啊");
    o.dropFillerOnly = true; o.fillerWords = {"a", "ab", "bc", ""};
    CHECK(normalizeText("abc", o).empty());
    o.fillerWords.clear(); CHECK(normalizeText("嗯", o) == "嗯");
}
void normalization_invalid_utf8()
{
    CHECK(normalizeText("a\xff\xe4\xb8", {}) == "a���");
    CHECK(normalizeText("\xf0\x9f\x98\x80", {}) == "😀");
    CHECK(normalizeText("\xed\xa0\x80", {}) == "���");
}
Segment segment(std::string text, int64_t durationMs = 4000)
{
    Segment s; s.text = std::move(text); s.begin16k = 16000; s.end16k = 16000 + durationMs * 16; return s;
}
HallucinationRules onlyTextRules()
{
    HallucinationRules r; r.minCharsPerSec = 0; r.maxCharsPerSec = 1e9; return r;
}
void hallucination_blacklist()
{
    auto r = onlyTextRules();
    CHECK(isLikelyHallucination(segment("<|zh|> 谢谢观看"), r));
    CHECK(!isLikelyHallucination(segment("他正在说谢谢观看这句话"), r));
    r.blacklist = {"<|zh|> 自定义黑名单"};
    CHECK(isLikelyHallucination(segment("自定义黑名单"), r));
    CHECK(!isLikelyHallucination(segment("谢谢观看"), r));
}
void hallucination_repetition()
{
    auto r = onlyTextRules();
    CHECK(!isLikelyHallucination(segment("你好你好你好你好"), r));
    CHECK(isLikelyHallucination(segment("开头你好你好你好你好你好结尾"), r));
    CHECK(isLikelyHallucination(segment("测试，测试，测试，测试，测试，"), r));
    CHECK(isLikelyHallucination(segment("go go go go go"), r));
    CHECK(!isLikelyHallucination(segment("测试，测试，正常句子。"), r));
    r.maxRepeat = 2;
    CHECK(isLikelyHallucination(segment("甲甲甲"), r));
}
void hallucination_speed()
{
    HallucinationRules r;
    CHECK(!isLikelyHallucination(segment("一二三四五六七八九十甲乙", 1000), r));
    CHECK(isLikelyHallucination(segment("一二三四五六七八九十甲乙丙", 1000), r));
    CHECK(!isLikelyHallucination(segment("一二三", 10000), r));
    CHECK(isLikelyHallucination(segment("一二", 10000), r));
    CHECK(!isLikelyHallucination(segment("extraordinarily longword123", 2000), r));
    CHECK(isLikelyHallucination(segment("测试", 0), r));
    CHECK(isLikelyHallucination(segment("测试", -1), r));
    CHECK(isLikelyHallucination(segment("嗯啊", 1000), r));
}
void hallucination_no_speech()
{
    auto s = segment("今天进行测试"); HallucinationRules r;
    CHECK(!isLikelyHallucination(s, r));
    s.noSpeechProb = .8f; CHECK(!isLikelyHallucination(s, r));
    s.noSpeechProb = .81f; CHECK(isLikelyHallucination(s, r));
    s.noSpeechProb = NAN; CHECK(!isLikelyHallucination(s, r));
}
FinalSegment final(std::string text, int64_t duration = 6000)
{
    FinalSegment f; f.id = 42; f.begin16k = 16000; f.end16k = (1000 + duration) * 16; f.text = std::move(text); return f;
}
std::string joined(const std::vector<Cue> &cues)
{
    std::string out; for (const auto &c : cues) out += c.text; return out;
}
void invariant(const std::vector<Cue> &cues, const FinalSegment &f, const CueConfig &cfg, const std::vector<int64_t> &boundaries = {})
{
    int64_t previous = f.begin16k / 16;
    for (const auto &c : cues) {
        CHECK(c.beginMs >= previous && c.endMs >= c.beginMs && c.endMs <= f.end16k / 16);
        CHECK(c.segmentId == f.id && !c.provisional && !c.edited);
        CHECK(effectiveCharCount(c.text) <= size_t(cfg.maxCharsPerLine * cfg.maxLines));
        CHECK(c.endMs - c.beginMs <= cfg.maxDurationMs);
        for (auto b : boundaries) CHECK(!(c.beginMs < b && c.endMs > b));
        previous = c.endMs;
    }
}
void cues_hard_boundaries()
{
    const auto f = final("一二三四五六七八九十甲乙"); CueConfig cfg;
    const std::vector<int64_t> boundaries{2500, 4000, 5101};
    auto cues = buildCues(f, cfg, boundaries);
    CHECK(cues.size() == 4 && joined(cues) == f.text);
    invariant(cues, f, cfg, boundaries);
    CHECK(buildCues(FinalSegment{}, cfg, {}).empty());
    auto token = final("indivisible", 3000); token.words = {{1000, 4000, "indivisible"}};
    cues = buildCues(token, cfg, {2000, 3000});
    CHECK(cues.size() == 1 && joined(cues) == token.text);
    invariant(cues, token, cfg, {2000, 3000});
    // Dense boundaries, duplicated/unsorted input and large timestamps.
    auto many = buildCues(f, cfg, {5101, 2500, 2500, 4000, 1000, 7000});
    CHECK(joined(many) == f.text); invariant(many, f, cfg, boundaries);
}
void cues_punctuation()
{
    CueConfig cfg; cfg.minDurationMs = 0;
    auto f = final("你好。世界！再见？结束；");
    auto cues = buildCues(f, cfg, {});
    CHECK(cues.size() == 4 && cues[0].text == "你好。" && joined(cues) == f.text);
    f = final("一二，三四五六七八"); cfg.maxCharsPerLine = 5;
    cues = buildCues(f, cfg, {});
    CHECK(cues.size() == 3 && cues[0].text == "一二，");
    invariant(cues, f, cfg);
    f = final("一，二三"); cfg.maxCharsPerLine = 18;
    CHECK(buildCues(f, cfg, {}).size() == 1);
}
void cues_char_count()
{
    CueConfig cfg; cfg.maxCharsPerLine = 2; cfg.maxLines = 2;
    auto f = final("一二三四五六七八九");
    auto cues = buildCues(f, cfg, {});
    CHECK(cues.size() == 3 && cues[0].text == "一二三四" && joined(cues) == f.text);
    invariant(cues, f, cfg);
    cfg.maxCharsPerLine = 1; cfg.maxLines = 1;
    f = final("supercalifragilistic 123456789 café");
    cues = buildCues(f, cfg, {});
    CHECK(cues.size() == 3 && cues[0].text == "supercalifragilistic" && cues[1].text == "123456789" && cues[2].text == "café");
    invariant(cues, f, cfg);
}
void cues_duration()
{
    CueConfig cfg; cfg.maxDurationMs = 2000; cfg.minDurationMs = 800;
    auto f = final("一二三四五六七八", 8000);
    auto cues = buildCues(f, cfg, {});
    CHECK(cues.size() == 4 && joined(cues) == f.text);
    invariant(cues, f, cfg);
    f = final("甲。乙。", 2000); f.words = {{1000, 1100, "甲"}, {2000, 2100, "乙"}};
    cues = buildCues(f, cfg, {});
    CHECK(cues.size() == 2 && cues[0].endMs == 1800 && cues[1].endMs == 2800);
    invariant(cues, f, cfg);
    // Hard boundaries take precedence over the minimum display duration.
    cues = buildCues(f, cfg, {1200});
    CHECK(cues.front().endMs == 1200); invariant(cues, f, cfg, {1200});
    f = final("一个词", 30000);
    cues = buildCues(f, cfg, {}); CHECK(joined(cues) == f.text); invariant(cues, f, cfg);
}
void cues_token_times()
{
    CueConfig cfg; cfg.minDurationMs = 0; cfg.maxCharsPerLine = 1;
    auto f = final("甲乙丙", 5000);
    f.words = {{1100, 1400, "甲", .8f}, {2500, 2800, "乙", .9f}, {4100, 4500, "丙", .7f}};
    auto cues = buildCues(f, cfg, {});
    CHECK(cues.size() == 3 && cues[0].beginMs == 1100 && cues[0].endMs == 1400);
    CHECK(cues[1].beginMs == 2500 && cues[2].endMs == 4500); invariant(cues, f, cfg);
    f.words = {{2000, 5000, "甲乙丙"}};
    cues = buildCues(f, cfg, {});
    CHECK(cues[0].beginMs == 2000 && cues[0].endMs == 3000 && cues[2].endMs == 5000);
    f.words[0].text = "不同文本";
    cues = buildCues(f, cfg, {});
    CHECK(cues[0].beginMs == 1000 && cues.back().endMs == 6000);
    f.words = {{-1, 999999, "甲乙丙"}};
    invariant(buildCues(f, cfg, {}), f, cfg);
}
void overlaps_monotonic()
{
    std::vector<Cue> cues{{100, 1000, "a", 1}, {-20, 30, "b", 2}, {100, 300, "c", 3}, {900, 800, "d", 4}};
    resolveOverlaps(cues);
    CHECK(cues.size() == 4 && cues[0].text == "b" && cues[0].beginMs == 0 && cues[1].text == "a");
    CHECK(cues[1].endMs == 100 && cues[3].endMs == 900);
    for (size_t i = 1; i < cues.size(); ++i) CHECK(cues[i].beginMs >= cues[i-1].endMs);
    const auto encoded = formatSrt(cues, false, false); resolveOverlaps(cues);
    CHECK(formatSrt(cues, false, false) == encoded);
}
void ass_format()
{
    AssStyle style; style.fontName = "Test,Font\n"; style.fontSize = 36; style.marginV = 24;
    auto output = formatAss({{1234, 360062345, "你好\n{\\b1} text"}, {10, 20, ""}}, style);
    CHECK(output.find("Style: Default,Test Font ,36,") != std::string::npos);
    CHECK(output.find("Dialogue: 0,0:00:01.23,100:01:02.34,Default,,0,0,0,,你好\\N\\{\\\\b1\\} text\r\n") != std::string::npos);
    CHECK(output.find("Dialogue:") == output.rfind("Dialogue:"));
}
void json_roundtrip()
{
    auto s = segment("中文😀\"\\\n\t");
    s.text += '\0';
    s.id = std::numeric_limits<int64_t>::max(); s.begin16k = std::numeric_limits<int64_t>::min();
    s.language = "zh"; s.noSpeechProb = .125f;
    s.words = {{10, 20, "中", .9f}, {20, 30, "文"}};
    std::vector<Segment> parsed;
    const auto encoded = toJson({s, Segment{}});
    CHECK(fromJson(encoded, parsed) && parsed.size() == 2);
    CHECK(toJson(parsed) == encoded && std::isnan(parsed[0].avgLogProb));
    CHECK(parsed[0].text == s.text && parsed[0].id == s.id && parsed[0].begin16k == s.begin16k);
    CHECK(parsed[0].words[0].prob == .9f && parsed[0].words[1].prob == -1.f);
    CHECK(fromJson("[]", parsed) && parsed.empty());
    CHECK(fromJson(R"([{"id":1,"begin16k":0,"end16k":16000,"text":"\u4e2d\ud83d\ude00","future":{"x":[true,null,1.2e3]}}])", parsed));
    CHECK(parsed[0].text == "中😀" && std::isnan(parsed[0].noSpeechProb));
}
void json_invalid()
{
    std::vector<Segment> out{segment("unchanged")};
    for (auto input : {"", "{}", "[{}]", "[1]", "[] junk", "[null]", "[true]", "[1,]",
        R"([{"id":1,"id":2}])", R"([{"id":1.5,"begin16k":0,"end16k":1,"text":"x"}])",
        R"([{"id":9223372036854775808,"begin16k":0,"end16k":1,"text":"x"}])",
        R"([{"id":1,"begin16k":0,"end16k":1,"text":"\ud800"}])",
        R"([{"id":1,"begin16k":0,"end16k":1,"text":"x","noSpeechProb":1e999}])"}) {
        CHECK(!fromJson(input, out)); CHECK(out[0].text == "unchanged");
    }
    CHECK(!fromJson(std::string(100, '[') + std::string(100, ']'), out));
}
void srt_roundtrip()
{
    std::vector<Cue> parsed;
    for (bool bom : {false, true}) for (bool crlf : {false, true}) {
        const auto s = formatSrt(goldenCues(), bom, crlf);
        CHECK(parseSrt(s, parsed) && parsed.size() == 2);
        CHECK(formatSrt(parsed, bom, crlf) == s);
        CHECK(parsed[1].beginMs == 360000001 && parsed[0].segmentId == -1 && !parsed[0].provisional);
    }
    CHECK(parseSrt("", parsed) && parsed.empty());
    CHECK(parseSrt("\xef\xbb\xbf", parsed) && parsed.empty());
    CHECK(parseSrt("1\n00:00:00,000 --> 00:00:01,000\nlast", parsed));
}
void srt_invalid()
{
    std::vector<Cue> out;
    for (auto input : {"garbage", "1\n", "0\n00:00:00,000 --> 00:00:01,000\nx", "1\n00:60:00,000 --> 01:00:01,000\nx",
        "1\n00:00:02,000 --> 00:00:01,000\nx", "1\n999999999999999999:00:00,000 --> 00:00:01,000\nx",
        "1\n00:00:00.000 --> 00:00:01,000\nx", "1\n00:00:00,000 --> 00:00:01,000\n\n"}) CHECK(!parseSrt(input, out));
}
void paths()
{
    const auto mp4 = std::filesystem::u8path("folder.with.dot/视频.name.mp4");
    const auto srt = srtPathForMp4(mp4);
    CHECK(srt == std::filesystem::u8path("folder.with.dot/视频.name.srt"));
    CHECK(srtPartPath(srt) == std::filesystem::u8path("folder.with.dot/视频.name.srt.part"));
    CHECK(liveSrtPath(srt) == std::filesystem::u8path("folder.with.dot/视频.name.live.srt"));
    CHECK(refinePartPath(srt) == std::filesystem::u8path("folder.with.dot/视频.name.srt.refine.part"));
    CHECK(srtPathForMp4("no_extension") == "no_extension.srt");
}
void atomic_success()
{
    Temp temp("atomic"); const auto path = temp.dir / std::filesystem::u8path("字幕.srt");
    CHECK(writeFileAtomic(path, "old").ok() && read(path) == "old");
    const std::string bytes("new\0value", 9);
    CHECK(writeFileAtomic(path, bytes).ok() && read(path) == bytes);
    CHECK(writeFileAtomic(path, "").ok() && read(path).empty());
    CHECK(!std::filesystem::exists(srtPartPath(path))); // no unrelated side effects
    CHECK(!std::filesystem::exists(path.string() + ".tmp"));
    CHECK(!writeFileAtomic(temp.dir / "missing" / "out.srt", "x").ok());
    CHECK(!writeFileAtomic({}, "x").ok());
    const auto directory = temp.dir / "directory"; std::filesystem::create_directory(directory);
    CHECK(writeFileAtomic(directory, "x").code == ErrorCode::SrtRenameFailed);
    CHECK(std::filesystem::is_directory(directory) && !std::filesystem::exists(directory.string() + ".tmp"));
    auto occupied = path; occupied += ".tmp";
    { std::ofstream file(occupied); file << "someone else's temporary file"; }
    CHECK(!writeFileAtomic(path, "replacement").ok() && read(path).empty());
    CHECK(read(occupied) == "someone else's temporary file");
}
int main(int argc, char **argv)
{
    const std::map<std::string, void(*)()> tests = {
#define TEST(name) {#name, name}
        TEST(golden_srt), TEST(golden_vtt), TEST(code_points), TEST(effective_characters), TEST(ascii_ratio),
        TEST(normalization_tags), TEST(normalization_fillers), TEST(normalization_invalid_utf8),
        TEST(hallucination_blacklist), TEST(hallucination_repetition), TEST(hallucination_speed), TEST(hallucination_no_speech),
        TEST(cues_hard_boundaries), TEST(cues_punctuation), TEST(cues_char_count), TEST(cues_duration), TEST(cues_token_times),
        TEST(overlaps_monotonic), TEST(ass_format), TEST(json_roundtrip), TEST(json_invalid), TEST(srt_roundtrip), TEST(srt_invalid), TEST(paths), TEST(atomic_success)
#undef TEST
    };
    try { CHECK(argc == 2); tests.at(argv[1])(); }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}

// 进程内假引擎单元测试（SUBREC_ASR_TEST_HOOKS=1 由 test_main 设置）。
// 进程级故障（crash/oom/hang/illegalInstr/garbageStdout）在 test_host_live.cpp 用子进程覆盖。

#include "test_framework.h"

#include "asr_engine.h"
#include "engine_fake.h"

#include "subtitle/subtitle_types.h"

#include <chrono>
#include <cmath>
#include <string>

using namespace asrhost;
using namespace subtitle;

namespace {

FakeAsrEngine makeEngine(const std::string &opts)
{
    FakeEngineOptions o;
    Status st;
    if (!parseFakeOptions(opts, o, &st)) {
        CHECK_MSG(false, "parseFakeOptions: " + st.message);
    }
    std::string backend;
    FakeAsrEngine e(o);
    Status lst = e.load(EngineRequest{}, &backend);
    if (!lst.ok())
        CHECK_MSG(false, "load: " + lst.message);
    return e;
}

FinalSegment recognizeN(FakeAsrEngine &e, int64_t segId, int64_t b16k, int64_t e16k,
                        RecognizeKind kind)
{
    FinalSegment io;
    io.id = segId;
    io.begin16k = b16k;
    io.end16k = e16k;
    std::vector<float> pcm(static_cast<size_t>(e16k - b16k), 0.1f);
    Status st = e.recognize(pcm.data(), pcm.size(), kind, io);
    CHECK_MSG(st.ok(), "recognize: " + st.message);
    return io;
}

size_t codepointCount(const std::string &s)
{
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        i += c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
        ++n;
    }
    return n;
}

size_t asciiBytes(const std::string &s)
{
    size_t n = 0;
    for (unsigned char c : s)
        if (c < 0x80)
            ++n;
    return n;
}

} // namespace

TEST(engine_final_text_and_words)
{
    auto e = makeEngine("");
    const FinalSegment f = recognizeN(e, 5, 16000, 16000 + 16000, RecognizeKind::Final);
    CHECK(f.text == "第5段");
    CHECK(f.language == "zh" && f.emotion.empty() && f.event.empty());
    CHECK(f.modelId == "fake");
    CHECK(f.decodeMs >= 0.f);
    // 每个码点一个 token；时间戳铺满 [begin16k, end16k) 的绝对毫秒区间
    CHECK(f.words.size() == codepointCount("第5段"));
    const int64_t baseMs = asrSamplesToMs(16000);
    const int64_t spanMs = asrSamplesToMs(16000);
    int64_t prevEnd = -1;
    for (const Word &w : f.words) {
        CHECK(w.beginMs >= baseMs && w.endMs <= baseMs + spanMs);
        CHECK(w.beginMs >= prevEnd);
        prevEnd = w.endMs;
        CHECK(w.prob < 0);
    }
    CHECK(prevEnd == baseMs + spanMs);
    // 拼接 token 文本还原整句
    std::string joined;
    for (const Word &w : f.words)
        joined += w.text;
    CHECK(joined == f.text);
}

TEST(engine_lang_en_mix)
{
    auto e = makeEngine("lang=en");
    const FinalSegment en = recognizeN(e, 1, 0, 16000, RecognizeKind::Final);
    CHECK(en.language == "en");
    CHECK(!en.text.empty());

    auto m = makeEngine("lang=mix");
    const FinalSegment mix = recognizeN(m, 1, 0, 16000, RecognizeKind::Final);
    CHECK(mix.language == "zh");
    const double ratio = static_cast<double>(asciiBytes(mix.text)) / mix.text.size();
    CHECK(ratio >= 0.30);
}

TEST(engine_partial_prefix_grows)
{
    auto e = makeEngine("");
    const FinalSegment p1 = recognizeN(e, 0, 0, 4000, RecognizeKind::Partial);     // 250 ms
    const FinalSegment p2 = recognizeN(e, 0, 0, 16000, RecognizeKind::Partial);    // 1000 ms
    const FinalSegment fin = recognizeN(e, 0, 0, 64000, RecognizeKind::Final);
    CHECK(codepointCount(p1.text) >= 1);
    CHECK(codepointCount(p2.text) >= codepointCount(p1.text));
    CHECK(codepointCount(fin.text) >= codepointCount(p2.text));
    // partial 文本是 final 文本的前缀
    CHECK(fin.text.compare(0, p2.text.size(), p2.text) == 0);
}

TEST(engine_partial_noise)
{
    auto clean = makeEngine("partialNoise=0");
    auto noisy = makeEngine("partialNoise=1,seed=11");
    const FinalSegment c = recognizeN(clean, 0, 0, 16000, RecognizeKind::Partial);
    const FinalSegment n = recognizeN(noisy, 0, 0, 16000, RecognizeKind::Partial);
    CHECK(n.text != c.text);   // p=1 必然改写尾部 1-3 个 token
    // 相同 seed 可复现
    auto noisy2 = makeEngine("partialNoise=1,seed=11");
    const FinalSegment n2 = recognizeN(noisy2, 0, 0, 16000, RecognizeKind::Partial);
    CHECK(n2.text == n.text);
}

TEST(engine_partial_regress)
{
    auto e = makeEngine("partialRegress=2");
    const FinalSegment p1 = recognizeN(e, 0, 0, 16000, RecognizeKind::Partial);
    const FinalSegment p2 = recognizeN(e, 0, 0, 24000, RecognizeKind::Partial);
    // 第 2 次 partial 改掉已出现的前缀（第 0 个码点被替换；UTF-8 首字节即可区分“改”与“第”）
    CHECK(!p1.text.empty() && !p2.text.empty());
    CHECK(p2.text[0] != p1.text[0]);
}

TEST(engine_final_differs)
{
    auto plain = makeEngine("finalDiffers=0");
    auto diff = makeEngine("finalDiffers=1,seed=3");
    const FinalSegment a = recognizeN(plain, 2, 0, 16000, RecognizeKind::Final);
    const FinalSegment b = recognizeN(diff, 2, 0, 16000, RecognizeKind::Final);
    CHECK(b.text != a.text);
    CHECK(b.text != "第2段" || a.text != "第2段");
}

TEST(engine_load_fail)
{
    FakeEngineOptions o;
    Status st;
    CHECK(parseFakeOptions("loadFail=303", o, &st));
    FakeAsrEngine e(o);
    std::string backend;
    Status ls = e.load(EngineRequest{}, &backend);
    CHECK(!ls.ok());
    CHECK(ls.code == ErrorCode::EngineLoadFailed);
}

TEST(engine_refine_empty)
{
    auto never = makeEngine("refineEmpty=0");
    const FinalSegment a = recognizeN(never, 0, 0, 16000, RecognizeKind::Refine);
    CHECK(!a.text.empty());

    auto always = makeEngine("refineEmpty=1,seed=5");
    const FinalSegment b = recognizeN(always, 0, 0, 16000, RecognizeKind::Refine);
    CHECK(b.text.empty() && b.words.empty());
}

TEST(engine_rtf_and_refine_rtf_sleep)
{
    const auto t0 = std::chrono::steady_clock::now();
    auto e = makeEngine("rtf=0.2,refineRtf=0.4");
    // 1000 ms 音频 × rtf 0.2 = 200 ms sleep
    recognizeN(e, 0, 0, 16000, RecognizeKind::Final);
    const auto ms1 = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    CHECK(ms1 >= 180);

    const auto t1 = std::chrono::steady_clock::now();
    // 1000 ms 音频 × refineRtf 0.4 = 400 ms sleep（同 rtf 不生效）
    recognizeN(e, 1, 0, 16000, RecognizeKind::Refine);
    const auto ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t1)
                        .count();
    CHECK(ms2 >= 380);
}

TEST(engine_seed_determinism)
{
    auto a = makeEngine("partialNoise=1,seed=42");
    auto b = makeEngine("partialNoise=1,seed=42");
    const FinalSegment pa = recognizeN(a, 0, 0, 16000, RecognizeKind::Partial);
    const FinalSegment pb = recognizeN(b, 0, 0, 16000, RecognizeKind::Partial);
    CHECK(pa.text == pb.text);
}

#include "encoder/video_encoder_tuning.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

using namespace video_tuning;

namespace {
void expectTier(int w, int h, int64_t expected)
{
    const int64_t got = targetBitRate(w, h);
    if (got != expected)
        throw std::runtime_error("targetBitRate(" + std::to_string(w) + "x" + std::to_string(h) + ") = "
                                 + std::to_string(got) + ", expected " + std::to_string(expected));
}

const char *optionValue(const EncoderAttempt &attempt, const char *key)
{
    for (const EncoderOption &option : attempt.options)
        if (std::strcmp(option.key, key) == 0)
            return option.value;
    return nullptr;
}

bool sameText(const char *a, const char *b) { return a && b && std::strcmp(a, b) == 0; }

void bitRateTiers()
{
    // 720p tier: everything up to and including 1280x720.
    expectTier(2, 2, 4'000'000);
    expectTier(640, 480, 4'000'000);
    expectTier(1279, 719, 4'000'000);
    expectTier(1279, 720, 4'000'000);
    expectTier(1280, 720, 4'000'000);
    expectTier(720, 1280, 4'000'000);
    // 1080p tier: above 1280x720 up to and including 1920x1080.
    expectTier(1281, 720, 8'000'000);
    expectTier(1366, 768, 8'000'000);
    expectTier(1919, 1079, 8'000'000);
    expectTier(1920, 1079, 8'000'000);
    expectTier(1920, 1080, 8'000'000);
    expectTier(1080, 1920, 8'000'000);
    // 1440p tier: above 1920x1080 up to and including 2560x1440.
    expectTier(1921, 1080, 12'000'000);
    expectTier(1920, 1200, 12'000'000);
    expectTier(2559, 1439, 12'000'000);
    expectTier(2560, 1439, 12'000'000);
    expectTier(2560, 1440, 12'000'000);
    // Larger: 20 Mbps.
    expectTier(2561, 1440, 20'000'000);
    expectTier(3440, 1440, 20'000'000);
    expectTier(3840, 2160, 20'000'000);
    expectTier(7680, 4320, 20'000'000);
    std::puts("PASS bit rate tiers: 4/8/12/20 Mbps, inclusive at 1280x720, 1920x1080, 2560x1440");
}

void rateLimits()
{
    for (const auto &size : {std::pair<int, int>{1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}}) {
        const RateLimits r = rateLimitsFor(size.first, size.second);
        CHECK(r.target == targetBitRate(size.first, size.second));
        CHECK(r.peak * 2 == r.target * 3); // 1.5x
        CHECK(r.vbv == r.target * 2);
    }
    const RateLimits hd = rateLimitsFor(1920, 1080);
    CHECK(hd.target == 8'000'000 && hd.peak == 12'000'000 && hd.vbv == 16'000'000);
    const RateLimits uhd = rateLimitsFor(3840, 2160);
    CHECK(uhd.peak == 30'000'000 && uhd.vbv == 40'000'000);
    std::puts("PASS rate limits: peak 1.5x, VBV 2x");
}

void families()
{
    CHECK(encoderFamily("h264_nvenc") == EncoderFamily::Nvenc);
    CHECK(encoderFamily("h264_amf") == EncoderFamily::Amf);
    CHECK(encoderFamily("h264_qsv") == EncoderFamily::Qsv);
    CHECK(encoderFamily("libx264") == EncoderFamily::X264);
}

void attempts()
{
    const std::string quality = std::to_string(kQualityLevel);

    const auto nvenc = encoderAttempts(EncoderFamily::Nvenc);
    CHECK(nvenc.size() == 3);
    CHECK(!nvenc[0].legacy && sameText(optionValue(nvenc[0], "preset"), "p5"));
    CHECK(sameText(optionValue(nvenc[0], "rc"), "vbr"));
    CHECK(sameText(optionValue(nvenc[0], "cq"), quality.c_str()));
    CHECK(sameText(optionValue(nvenc[0], "tune"), "hq"));
    CHECK(sameText(optionValue(nvenc[1], "preset"), "p4") && !nvenc[1].legacy);
    CHECK(nvenc[2].legacy && sameText(optionValue(nvenc[2], "rc"), "cbr"));

    const auto amf = encoderAttempts(EncoderFamily::Amf);
    CHECK(amf.size() == 2);
    CHECK(!amf[0].legacy && sameText(optionValue(amf[0], "quality"), "balanced"));
    CHECK(sameText(optionValue(amf[0], "rc"), "vbr_peak"));
    CHECK(amf[1].legacy);

    const auto qsv = encoderAttempts(EncoderFamily::Qsv);
    CHECK(qsv.size() == 3);
    CHECK(!qsv[0].legacy && qsv[0].globalQuality == kQualityLevel);
    CHECK(sameText(optionValue(qsv[0], "preset"), "medium"));
    CHECK(qsv[1].globalQuality == 0 && !qsv[1].legacy);
    CHECK(qsv[2].legacy);

    const auto x264 = encoderAttempts(EncoderFamily::X264);
    CHECK(x264.size() == 1 && !x264[0].legacy);
    CHECK(sameText(optionValue(x264[0], "preset"), "veryfast"));
    CHECK(sameText(optionValue(x264[0], "crf"), quality.c_str()));
    CHECK(sameText(optionValue(x264[0], "tune"), "zerolatency"));
    CHECK(optionValue(x264[0], "x264-params") == nullptr); // default x264 tools

    for (EncoderFamily family : {EncoderFamily::Nvenc, EncoderFamily::Amf, EncoderFamily::Qsv, EncoderFamily::X264}) {
        const auto list = encoderAttempts(family);
        CHECK(!list.empty() && !list.front().legacy); // capped quality is always tried first
        for (std::size_t i = 0; i + 1 < list.size(); ++i)
            CHECK(!list[i].legacy); // legacy only as the last resort
    }
    std::puts("PASS encoder attempts: capped quality first, legacy CBR/VBR last");
}
} // namespace

int main()
{
    try {
        bitRateTiers();
        rateLimits();
        families();
        attempts();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL %s\n", e.what());
        return 1;
    }
    return 0;
}

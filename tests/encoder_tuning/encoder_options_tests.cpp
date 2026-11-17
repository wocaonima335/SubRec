// Checks every encoder attempt against the bundled FFmpeg: option names and constant values
// must exist in the encoder's own AVOption table (no hardware needed), and libx264 must open
// with its capped-CRF settings. Encoders missing from the FFmpeg build are skipped.
#include "encoder/video_encoder_setup.h"

#include <cstdio>
#include <stdexcept>
#include <string>

extern "C" {
#include <libavutil/opt.h>
}

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

namespace {
struct ContextGuard
{
    AVCodecContext *ctx = nullptr;
    ~ContextGuard() { avcodec_free_context(&ctx); }
};

struct DictGuard
{
    AVDictionary *dict = nullptr;
    ~DictGuard() { av_dict_free(&dict); }
};

// Returns false when the encoder is not part of this FFmpeg build.
bool checkOptionTables(const char *codecName)
{
    const AVCodec *codec = avcodec_find_encoder_by_name(codecName);
    if (!codec) {
        std::printf("SKIP %s: not in this FFmpeg build\n", codecName);
        return false;
    }
    const video_tuning::RateLimits rates = video_tuning::rateLimitsFor(1920, 1080);
    for (const video_tuning::EncoderAttempt &attempt :
         video_tuning::encoderAttempts(video_tuning::encoderFamily(codecName))) {
        ContextGuard ctx{avcodec_alloc_context3(codec)};
        CHECK(ctx.ctx && ctx.ctx->priv_data);
        DictGuard options{applyEncoderAttempt(ctx.ctx, attempt, rates)};
        CHECK(ctx.ctx->bit_rate == rates.target);
        if (!attempt.legacy) {
            CHECK(ctx.ctx->rc_max_rate == rates.peak);
            CHECK(ctx.ctx->rc_buffer_size == rates.vbv);
        }
        const int count = av_dict_count(options.dict);
        CHECK(count == static_cast<int>(attempt.options.size()));
        // Consumed entries are removed; unknown keys stay, bad constant values fail.
        const int ret = av_opt_set_dict(ctx.ctx->priv_data, &options.dict);
        if (ret < 0 || av_dict_count(options.dict) != 0) {
            const AVDictionaryEntry *left = av_dict_get(options.dict, "", nullptr, AV_DICT_IGNORE_SUFFIX);
            throw std::runtime_error(std::string(codecName) + "/" + attempt.profile + ": option rejected ("
                                     + (left ? left->key : "bad value") + ")");
        }
        std::printf("PASS %s/%s: %d options known to the encoder\n", codecName, attempt.profile, count);
    }
    return true;
}

bool openLibx264()
{
    const AVCodec *codec = avcodec_find_encoder_by_name("libx264");
    if (!codec)
        return false;
    const video_tuning::RateLimits rates = video_tuning::rateLimitsFor(1920, 1080);
    const auto attempts = video_tuning::encoderAttempts(video_tuning::EncoderFamily::X264);
    ContextGuard ctx{avcodec_alloc_context3(codec)};
    CHECK(ctx.ctx);
    ctx.ctx->width = 64;
    ctx.ctx->height = 64;
    ctx.ctx->framerate = AVRational{30, 1};
    ctx.ctx->time_base = AVRational{1, 30};
    ctx.ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx.ctx->max_b_frames = 0;
    ctx.ctx->gop_size = 30;
    DictGuard options{applyEncoderAttempt(ctx.ctx, attempts.front(), rates)};
    const int ret = avcodec_open2(ctx.ctx, codec, &options.dict);
    CHECK(ret >= 0);
    CHECK(av_dict_count(options.dict) == 0);
    CHECK(ctx.ctx->rc_max_rate == rates.peak);
    std::puts("PASS libx264 opens with veryfast/crf 23 and a 12 Mbps VBV cap (1080p tier)");
    return true;
}
} // namespace

int main()
{
    try {
        int checked = 0;
        for (const char *name : {"h264_nvenc", "h264_amf", "h264_qsv", "libx264"})
            checked += checkOptionTables(name) ? 1 : 0;
        openLibx264();
        if (checked == 0) {
            std::puts("SKIP no H.264 encoders in this FFmpeg build");
            return 77;
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL %s\n", e.what());
        return 1;
    }
    return 0;
}

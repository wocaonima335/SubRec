#ifndef VIDEO_ENCODER_TUNING_H
#define VIDEO_ENCODER_TUNING_H

// Rate control and preset choices for the H.264 video encoders. Header-only and free of
// FFmpeg types so the tables can be unit tested (tests/encoder_tuning) and shared by the
// encoder (FFVEncoder::initVideo) and the ddagrab GPU-frame probe.

#include <cstdint>
#include <cstring>
#include <vector>

namespace video_tuning {

// Quality target for the capped-quality modes (NVENC cq, QSV QVBR, x264 crf).
inline constexpr int kQualityLevel = 23;

struct RateLimits
{
    int64_t target = 0; // average bit rate, bit/s
    int64_t peak = 0;   // rc_max_rate
    int64_t vbv = 0;    // rc_buffer_size
};

// Target bit rate at 30 fps, chosen by pixel count. Each tier covers every size up to and
// including its reference resolution (reference >= pixels), so exactly 1920x1080 lands in
// the 8 Mbps tier and 1919x1079 too; 1921x1080 moves up to 12 Mbps.
inline int64_t targetBitRate(int width, int height)
{
    struct Tier
    {
        int64_t referencePixels;
        int64_t bitRate;
    };
    static constexpr Tier tiers[] = {
        {int64_t{1280} * 720, 4'000'000},
        {int64_t{1920} * 1080, 8'000'000},
        {int64_t{2560} * 1440, 12'000'000},
    };
    const int64_t pixels = int64_t{width} * height;
    for (const Tier &tier : tiers) {
        if (tier.referencePixels >= pixels)
            return tier.bitRate;
    }
    return 20'000'000;
}

// Peak 1.5x the target, VBV two seconds' worth of the target.
inline RateLimits rateLimitsFor(int width, int height)
{
    const int64_t target = targetBitRate(width, height);
    return {target, target * 3 / 2, target * 2};
}

enum class EncoderFamily { Nvenc, Amf, Qsv, X264 };

inline EncoderFamily encoderFamily(const char *codecName)
{
    if (std::strstr(codecName, "nvenc"))
        return EncoderFamily::Nvenc;
    if (std::strstr(codecName, "amf"))
        return EncoderFamily::Amf;
    if (std::strstr(codecName, "qsv"))
        return EncoderFamily::Qsv;
    return EncoderFamily::X264;
}

struct EncoderOption
{
    const char *key;
    const char *value;
};

// One way to open an encoder. Attempts are tried in order; the first that opens wins.
struct EncoderAttempt
{
    const char *profile;    // log label
    bool legacy = false;    // pre-quality-A settings: 1.2x peak, 1x VBV, QP clamps
    int globalQuality = 0;  // >0: AVCodecContext::global_quality
    std::vector<EncoderOption> options;
};

// Capped quality first. Hardware encoders keep the old CBR settings (with the new bit rate
// table) as a last resort, because older drivers or hardware may reject the newer modes.
inline std::vector<EncoderAttempt> encoderAttempts(EncoderFamily family)
{
    switch (family) {
    case EncoderFamily::Nvenc:
        return {
            {"p5-cq", false, 0,
             {{"preset", "p5"}, {"tune", "hq"}, {"rc", "vbr"}, {"cq", "23"}, {"spatial-aq", "1"}}},
            {"p4-cq", false, 0,
             {{"preset", "p4"}, {"tune", "hq"}, {"rc", "vbr"}, {"cq", "23"}}},
            {"legacy-cbr", true, 0, {{"preset", "fast"}, {"rc", "cbr"}}},
        };
    case EncoderFamily::Amf:
        return {
            {"balanced-vbr-peak", false, 0, {{"quality", "balanced"}, {"rc", "vbr_peak"}}},
            {"legacy-cbr", true, 0, {{"quality", "speed"}, {"rc", "cbr"}}},
        };
    case EncoderFamily::Qsv:
        // bit_rate + rc_max_rate != bit_rate + global_quality selects QVBR in qsvenc.
        return {
            {"medium-qvbr", false, kQualityLevel, {{"preset", "medium"}}},
            {"medium-vbr", false, 0, {{"preset", "medium"}}},
            {"legacy-vbr", true, 0, {{"preset", "fast"}}},
        };
    case EncoderFamily::X264:
        // crf replaces ABR in libx264; rc_max_rate/rc_buffer_size stay as the VBV cap.
        // Default x264 tools (no x264-params overrides).
        return {
            {"veryfast-crf", false, 0,
             {{"preset", "veryfast"}, {"tune", "zerolatency"}, {"crf", "23"}}},
        };
    }
    return {};
}

} // namespace video_tuning

#endif // VIDEO_ENCODER_TUNING_H

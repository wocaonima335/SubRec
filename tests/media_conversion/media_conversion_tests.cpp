#include "capture/media/media_conversion.h"

#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>
extern "C" {
#include <libavutil/version.h>
}
using namespace capture::media;

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " CHECK(" #condition ") failed"); } while (false)

template<class Exception, class Function> void throws(Function&& function) {
    bool caught = false;
    try { function(); } catch (const Exception&) { caught = true; }
    CHECK(caught);
}
const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
AudioFormat pcmFormat(AVSampleFormat format = AV_SAMPLE_FMT_S16, int rate = 44100) {
    return AudioFormat(rate, format, stereo, {1, rate});
}
FramePtr pcm(const AudioFormat& format, int samples, int64_t pts) {
    auto frame = makeFrame();
    frame->format = format.sampleFormat();
    frame->sample_rate = format.sampleRate();
    frame->nb_samples = samples;
    frame->pts = pts;
    CHECK(av_channel_layout_copy(&frame->ch_layout, &format.channelLayout()) == 0);
    CHECK(av_frame_get_buffer(frame.get(), 32) == 0);
    const int channels = format.channelLayout().nb_channels;
    const bool planar = av_sample_fmt_is_planar(format.sampleFormat()) != 0;
    const int bytes = av_get_bytes_per_sample(format.sampleFormat());
    for (int i = 0; i < samples; ++i) for (int channel = 0; channel < channels; ++channel) {
        const double value = channel == 0 ? 0.25 : -0.125;
        uint8_t* target = frame->extended_data[planar ? channel : 0]
                          + (planar ? i : i * channels + channel) * bytes;
        switch (av_get_packed_sample_fmt(format.sampleFormat())) {
        case AV_SAMPLE_FMT_U8: { const uint8_t v = static_cast<uint8_t>(128 + value * 128); std::memcpy(target, &v, bytes); break; }
        case AV_SAMPLE_FMT_S16: { const int16_t v = static_cast<int16_t>(value * 32768); std::memcpy(target, &v, bytes); break; }
        case AV_SAMPLE_FMT_S32: { const int32_t v = static_cast<int32_t>(value * 2147483648.0); std::memcpy(target, &v, bytes); break; }
        case AV_SAMPLE_FMT_S64: { const int64_t v = static_cast<int64_t>(value * 9223372036854775808.0); std::memcpy(target, &v, bytes); break; }
        case AV_SAMPLE_FMT_FLT: { const float v = static_cast<float>(value); std::memcpy(target, &v, bytes); break; }
        case AV_SAMPLE_FMT_DBL: { std::memcpy(target, &value, bytes); break; }
        default: CHECK(false);
        }
    }
    return frame;
}
struct Bgra {
    int stride;
    std::vector<uint8_t> bytes;
    FramePtr frame = makeFrame();
    Bgra(int width, int height, int padding, bool negative = false)
        : stride(width * 4 + padding), bytes(static_cast<size_t>(stride) * height + 64, 0xCD) {
        frame->width = width; frame->height = height; frame->format = AV_PIX_FMT_BGRA;
        frame->pts = 1000; frame->duration = 40;
        frame->linesize[0] = negative ? -stride : stride;
        frame->data[0] = bytes.data() + (negative ? static_cast<size_t>(stride) * (height - 1) : 0);
        const std::array<std::array<uint8_t, 4>, 4> colors{{
            {{0, 0, 255, 255}}, {{0, 255, 0, 255}}, {{255, 0, 0, 255}}, {{255, 255, 255, 255}}
        }};
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const int block = (y >= height / 2 ? 2 : 0) + (x >= width / 2 ? 1 : 0);
            std::memcpy(frame->data[0] + y * frame->linesize[0] + x * 4, colors[block].data(), 4);
        }
    }
};
VideoConverter video(int width = 32, int height = 24) {
    return VideoConverter(VideoFormat(64, 48, AV_PIX_FMT_BGRA, {1, 1000}),
                          VideoFormat(width, height, AV_PIX_FMT_YUV420P, {1, 90000}));
}
void checkColors(const AVFrame& frame) {
    const int expected[4][3] = {{81, 90, 240}, {145, 54, 34}, {41, 240, 110}, {235, 128, 128}};
    for (int block = 0; block < 4; ++block) {
        const int x = (block % 2 == 0 ? 1 : 3) * frame.width / 4;
        const int y = (block < 2 ? 1 : 3) * frame.height / 4;
        for (int plane = 0; plane < 3; ++plane) {
            const int sx = plane ? x / 2 : x, sy = plane ? y / 2 : y;
            CHECK(std::abs(int(frame.data[plane][sy * frame.linesize[plane] + sx]) - expected[block][plane]) <= 3);
        }
    }
}
void equalPixels(const AVFrame& a, const AVFrame& b) {
    CHECK(a.width == b.width && a.height == b.height);
    for (int plane = 0; plane < 3; ++plane) {
        const int width = plane ? a.width / 2 : a.width;
        const int height = plane ? a.height / 2 : a.height;
        for (int y = 0; y < height; ++y)
            CHECK(std::memcmp(a.data[plane] + y * a.linesize[plane], b.data[plane] + y * b.linesize[plane], width) == 0);
    }
}
void videoStrideResize() {
    Bgra tight(64, 48, 0), padded(64, 48, 28), inverted(64, 48, 28, true);
    const auto originalBytes = padded.bytes;
    auto converter = video();
    auto a = converter.convert(*tight.frame);
    auto b = converter.convert(*padded.frame);
    auto c = converter.convert(*inverted.frame);
    CHECK(a->format == AV_PIX_FMT_YUV420P && a->width == 32 && a->height == 24);
    CHECK(a->pts == 90000 && a->duration == 3600 && a->time_base.den == 90000);
    CHECK(a->colorspace == AVCOL_SPC_SMPTE170M && a->color_range == AVCOL_RANGE_MPEG);
    CHECK(a->data[0] != b->data[0]);
    CHECK(padded.bytes == originalBytes);
    equalPixels(*a, *b); equalPixels(*a, *c); checkColors(*a);
    auto sameSize = video(64, 48);
    auto full = sameSize.convert(*padded.frame);
    checkColors(*full);
    auto upscale = video(128, 96);
    auto large = upscale.convert(*padded.frame);
    checkColors(*large);
    padded.frame->pts = AV_NOPTS_VALUE;
    CHECK(converter.convert(*padded.frame)->pts == AV_NOPTS_VALUE);
    // Outputs remain readable after their converter is destroyed.
    FramePtr retained;
    { auto temporary = video(); retained = temporary.convert(*tight.frame); }
    checkColors(*retained);
}
void channelLayoutCopy() {
    AVChannelLayout raw{};
    CHECK(av_channel_layout_custom_init(&raw, 2) == 0);
    raw.u.map[0].id = AV_CHAN_FRONT_LEFT;
    raw.u.map[1].id = AV_CHAN_FRONT_RIGHT;
    std::strcpy(raw.u.map[0].name, "left");
    AudioFormat a(44100, AV_SAMPLE_FMT_S16, raw, {1, 44100});
    CHECK(a.channelLayout().u.map != raw.u.map);
    AudioFormat b(a);
    auto c = pcmFormat(); c = a;
    CHECK(b.channelLayout().u.map != a.channelLayout().u.map);
    CHECK(c.channelLayout().u.map != a.channelLayout().u.map);
    raw.u.map[0].name[0] = 'X';
    av_channel_layout_uninit(&raw);
    CHECK(std::strcmp(a.channelLayout().u.map[0].name, "left") == 0);
    CHECK(av_channel_layout_compare(&b.channelLayout(), &c.channelLayout()) == 0);
    b = b;
    AudioFormat moved(std::move(b));
    CHECK(moved.channelLayout().nb_channels == 2);
    throws<std::logic_error>([&] { (void)b.channelLayout(); });
    b = a;
    c = std::move(b);
    CHECK(std::strcmp(c.channelLayout().u.map[0].name, "left") == 0);
    // Exercise the owned custom layout after the original was released.
    AudioConverter converter(c);
    auto input = pcm(c, 4410, 0);
    auto output = converter.convert(*input);
    CHECK(output && output->ch_layout.nb_channels == 2);
}
void inspectAudio(const AVFrame& output, int64_t expectedPts) {
    CHECK(output.format == AV_SAMPLE_FMT_FLTP && output.sample_rate == 48000);
    CHECK(output.ch_layout.nb_channels == 2 && av_channel_layout_compare(&output.ch_layout, &stereo) == 0);
    CHECK(output.nb_samples > 0 && output.duration == output.nb_samples);
    CHECK(output.time_base.num == 1 && output.time_base.den == 48000);
    CHECK(output.pts == expectedPts);
    CHECK(output.extended_data[0] && output.extended_data[1]);
    for (int channel = 0; channel < 2; ++channel) {
        const float* samples = reinterpret_cast<const float*>(output.extended_data[channel]);
        for (int i = 0; i < output.nb_samples; ++i) {
            CHECK(std::isfinite(samples[i]));
            CHECK(std::abs(samples[i] - (channel == 0 ? 0.25f : -0.125f)) < 0.015f);
        }
    }
}
void audioRateFlush() {
    auto format = pcmFormat();
    AudioConverter converter(format);
    int64_t total = 0, beforeDrain = 0;
    FramePtr retained;
    for (int block = 0; block < 100; ++block) {
        auto input = pcm(format, 441, 88200 + block * 441);
        auto output = converter.convert(*input);
        if (output) { inspectAudio(*output, 96000 + total); total += output->nb_samples; retained = std::move(output); }
    }
    beforeDrain = total;
    CHECK(beforeDrain < 48000);
    int drainFrames = 0;
    while (auto output = converter.flush()) {
        CHECK(++drainFrames < 20);
        inspectAudio(*output, 96000 + total);
        total += output->nb_samples;
    }
    CHECK(total == 48000 && total > beforeDrain);
    CHECK(!converter.drain() && !converter.flush());
    CHECK(retained && retained->data[0]);
    auto input = pcm(format, 441, 132300);
    throws<std::logic_error>([&] { converter.convert(*input); });
    std::cout << "  44100 input samples -> " << total << " output; before drain=" << beforeDrain
              << ", tail=" << total - beforeDrain << ", drain frames=" << drainFrames << '\n';
}
void pcmFormats() {
    const AVSampleFormat formats[] = {AV_SAMPLE_FMT_U8, AV_SAMPLE_FMT_U8P,
        AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S32P,
        AV_SAMPLE_FMT_S64, AV_SAMPLE_FMT_S64P, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBLP};
    for (auto sampleFormat : formats) {
        auto format = pcmFormat(sampleFormat);
        AudioConverter converter(format);
        auto input = pcm(format, 4410, 0);
        int total = 0;
        if (auto output = converter.convert(*input)) { inspectAudio(*output, total); total += output->nb_samples; }
        int calls = 0;
        while (auto output = converter.drain()) {
            CHECK(++calls < 20); inspectAudio(*output, total); total += output->nb_samples;
        }
        CHECK(total == 4800);
    }
    std::cout << "  12 packed/planar PCM formats passed\n";
}
void audioBufferingAndUnknownPts() {
    auto format = pcmFormat();
    AudioConverter empty(format);
    CHECK(!empty.drain() && !empty.drain());
    AudioConverter converter(format);
    auto one = pcm(format, 1, AV_NOPTS_VALUE);
    CHECK(!converter.convert(*one));
    auto rest = pcm(format, 4409, AV_NOPTS_VALUE);
    int total = 0;
    if (auto output = converter.convert(*rest)) { inspectAudio(*output, AV_NOPTS_VALUE); total += output->nb_samples; }
    int calls = 0;
    while (auto output = converter.drain()) {
        CHECK(++calls < 20); inspectAudio(*output, AV_NOPTS_VALUE); total += output->nb_samples;
    }
    CHECK(total == 4800);
    // Converter copies its description and returned frames own their storage.
    FramePtr owned;
    { AudioConverter local(pcmFormat(AV_SAMPLE_FMT_FLTP, 48000));
      auto input = pcm(local.outputFormat(), 128, -128);
      owned = local.convert(*input); }
    CHECK(owned); inspectAudio(*owned, -128);
}
void invalidVideo() {
    throws<std::invalid_argument>([] { VideoFormat(0, 48, AV_PIX_FMT_BGRA, {1, 1000}); });
    throws<std::invalid_argument>([] { VideoFormat(31, 24, AV_PIX_FMT_YUV420P, {1, 1000}); });
    throws<std::invalid_argument>([] { VideoFormat(64, 48, AV_PIX_FMT_NV12, {1, 1000}); });
    throws<std::invalid_argument>([] { VideoFormat(64, 48, AV_PIX_FMT_BGRA, {0, 1000}); });
    throws<std::invalid_argument>([] { VideoConverter c(VideoFormat(64, 48, AV_PIX_FMT_YUV420P, {1, 1}), VideoFormat(64, 48, AV_PIX_FMT_BGRA, {1, 1})); });
    auto converter = video(); Bgra input(64, 48, 28);
    input.frame->width = 32;
    throws<std::invalid_argument>([&] { converter.convert(*input.frame); });
    input.frame->width = 64; input.frame->linesize[0] = 255;
    throws<std::invalid_argument>([&] { converter.convert(*input.frame); });
    input.frame->linesize[0] = INT32_MIN;
    throws<std::invalid_argument>([&] { converter.convert(*input.frame); });
    input.frame->linesize[0] = input.stride;
    auto pointer = input.frame->data[0]; input.frame->data[0] = nullptr;
    throws<std::invalid_argument>([&] { converter.convert(*input.frame); });
    input.frame->data[0] = pointer; input.frame->duration = -1;
    throws<std::invalid_argument>([&] { converter.convert(*input.frame); });
    input.frame->duration = 0;
    CHECK(converter.convert(*input.frame));
}
void invalidAudio() {
    throws<std::invalid_argument>([] { AudioFormat(0, AV_SAMPLE_FMT_S16, stereo, {1, 1}); });
    throws<std::invalid_argument>([] { AudioFormat(44100, AV_SAMPLE_FMT_NONE, stereo, {1, 44100}); });
    throws<std::invalid_argument>([] { AVChannelLayout invalid{}; AudioFormat(44100, AV_SAMPLE_FMT_S16, invalid, {1, 44100}); });
    throws<std::invalid_argument>([] { AVChannelLayout unspecified{}; unspecified.nb_channels = 2;
        AudioConverter c(AudioFormat(44100, AV_SAMPLE_FMT_S16, unspecified, {1, 44100})); });
    auto format = pcmFormat(AV_SAMPLE_FMT_S16P); AudioConverter converter(format);
    auto input = pcm(format, 441, 0);
    input->sample_rate = 48000;
    throws<std::invalid_argument>([&] { converter.convert(*input); });
    input->sample_rate = 44100; input->format = AV_SAMPLE_FMT_FLTP;
    throws<std::invalid_argument>([&] { converter.convert(*input); });
    input->format = AV_SAMPLE_FMT_S16P;
    const AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
    CHECK(av_channel_layout_copy(&input->ch_layout, &mono) == 0);
    throws<std::invalid_argument>([&] { converter.convert(*input); });
    CHECK(av_channel_layout_copy(&input->ch_layout, &stereo) == 0);
    input->nb_samples = 0;
    throws<std::invalid_argument>([&] { converter.convert(*input); });
    input->nb_samples = INT32_MAX;
    throws<std::invalid_argument>([&] { converter.convert(*input); });
    input->nb_samples = 441;
    auto pointer = input->extended_data[1]; input->extended_data[1] = nullptr;
    throws<std::invalid_argument>([&] { converter.convert(*input); });
    input->extended_data[1] = pointer;
    const int line = input->linesize[0]; input->linesize[0] = 2;
    throws<std::invalid_argument>([&] { converter.convert(*input); });
    input->linesize[0] = line;
    CHECK(converter.convert(*input));
    input->pts = 10000;
    throws<std::invalid_argument>([&] { converter.convert(*input); });
    input->pts = AV_NOPTS_VALUE;
    throws<std::invalid_argument>([&] { converter.convert(*input); });
    input->pts = 441;
    CHECK(converter.convert(*input));
}
void timestampTests() {
    CHECK(rescaleTimestamp(1000, {1, 1000}, {1, 48000}) == 48000);
    CHECK(rescaleTimestamp(-1000, {1, 1000}, {1, 48000}) == -48000);
    CHECK(rescaleTimestamp(1, {1, 2}, {1, 1}) == 1);
    CHECK(rescaleTimestamp(-1, {1, 2}, {1, 1}) == -1);
    CHECK(rescaleTimestamp(AV_NOPTS_VALUE, {1, 1000}, {1, 48000}) == AV_NOPTS_VALUE);
    throws<std::invalid_argument>([] { rescaleTimestamp(1, {1, 0}, {1, 1}); });
    throws<std::invalid_argument>([] { rescaleTimestamp(AV_NOPTS_VALUE, {1, 1}, {-1, 1}); });
    throws<std::overflow_error>([] { rescaleTimestamp(INT64_MAX, {2, 1}, {1, 1}); });
    auto format = AudioFormat(44100, AV_SAMPLE_FMT_S16, stereo, {1, 1000});
    AudioConverter converter(format);
    int64_t total = 0;
    for (int block = 0; block < 10; ++block) {
        auto input = pcm(format, 441, 1000 + block * 10);
        if (auto output = converter.convert(*input)) { inspectAudio(*output, 48000 + total); total += output->nb_samples; }
    }
    while (auto output = converter.drain()) { inspectAudio(*output, 48000 + total); total += output->nb_samples; }
    CHECK(total == 4800);
}
int main() {
    std::cout << "FFmpeg headers avutil=" << LIBAVUTIL_VERSION_INT << ", runtime=" << avutil_version()
              << "; swscale=" << swscale_version() << "; swresample=" << swresample_version() << '\n';
#ifdef NDEBUG
    std::cout << "NDEBUG defined: explicit CHECK remains enabled\n";
#endif
    const std::pair<const char*, std::function<void()>> tests[] = {
        {"video color blocks/stride/resize/ownership", videoStrideResize},
        {"channel layout deep copy/move", channelLayoutCopy},
        {"44100 stereo -> 48000 FLTP + flush", audioRateFlush},
        {"all common packed/planar PCM formats", pcmFormats},
        {"buffering/unknown PTS/empty drain/ownership", audioBufferingAndUnknownPts},
        {"invalid and unsupported video", invalidVideo},
        {"invalid and discontinuous audio", invalidAudio},
        {"time base conversion/overflow", timestampTests}
    };
    int failed = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
    }
    std::cout << (8 - failed) << "/8 test groups passed\n";
    return failed == 0 ? 0 : 1;
}

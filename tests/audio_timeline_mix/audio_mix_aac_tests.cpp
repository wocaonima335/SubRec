#include "filter/audio_timeline_mixer.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
}
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr int rate = 48000;
constexpr double pi = 3.14159265358979323846;
using Audio = std::array<std::vector<float>, 2>;
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
void check(int result) {
    if (result < 0) {
        char message[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(result, message, sizeof(message));
        throw std::runtime_error(message);
    }
}
struct CodecDelete { void operator()(AVCodecContext *p) const { avcodec_free_context(&p); } };
struct PacketDelete { void operator()(AVPacket *p) const { av_packet_free(&p); } };
struct SwrDelete { void operator()(SwrContext *p) const { swr_free(&p); } };
using Codec = std::unique_ptr<AVCodecContext, CodecDelete>;
AudioTimelineMixer::Frame frame(int samples, int64_t pts) {
    AudioTimelineMixer::Frame f(av_frame_alloc());
    require(bool(f), "allocate frame");
    f->sample_rate = rate; f->format = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_default(&f->ch_layout, 2);
    f->nb_samples = samples; f->pts = pts; f->time_base = {1,rate};
    check(av_frame_get_buffer(f.get(), 0));
    return f;
}
void append(Audio &audio, const AVFrame &f) {
    require(f.format == AV_SAMPLE_FMT_FLTP && f.ch_layout.nb_channels == 2, "expected stereo FLTP");
    for (int ch = 0; ch < 2; ++ch) {
        auto *p = reinterpret_cast<const float *>(f.extended_data[ch]);
        audio[ch].insert(audio[ch].end(), p, p + f.nb_samples);
    }
}
Audio roundTrip(const Audio &input) {
    const auto *encoder = avcodec_find_encoder_by_name("aac");
    const auto *decoder = avcodec_find_decoder_by_name("aac");
    require(encoder, "native aac encoder required");
    require(decoder, "aac decoder missing: rebuild FFmpeg with SUBREC_FFMPEG_TEST_COMPONENTS=1 (--enable-decoder=aac)");
    Codec enc(avcodec_alloc_context3(encoder)), dec(avcodec_alloc_context3(decoder));
    require(enc && dec, "allocate codecs");
    // Match FFAEncoder::initAudio exactly, including 64*1024 (not 64000).
    enc->bit_rate = 64 * 1024; enc->sample_rate = rate;
    enc->sample_fmt = AV_SAMPLE_FMT_FLTP; enc->time_base = {1,rate};
    av_channel_layout_default(&enc->ch_layout, 2);
    enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    check(avcodec_open2(enc.get(), encoder, nullptr));
    AVCodecParameters *parameters = avcodec_parameters_alloc();
    require(parameters, "allocate codec parameters");
    int result = avcodec_parameters_from_context(parameters, enc.get());
    if (result >= 0) result = avcodec_parameters_to_context(dec.get(), parameters);
    avcodec_parameters_free(&parameters); check(result);
    check(avcodec_open2(dec.get(), decoder, nullptr));
    std::unique_ptr<AVPacket, PacketDelete> packet(av_packet_alloc());
    AudioTimelineMixer::Frame decoded(av_frame_alloc());
    require(packet && decoded, "allocate decode buffers");
    Audio output;
    auto drainDecoder = [&] {
        int ret;
        while ((ret = avcodec_receive_frame(dec.get(), decoded.get())) >= 0) {
            append(output, *decoded); av_frame_unref(decoded.get());
        }
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) check(ret);
        return ret;
    };
    auto drainEncoder = [&] {
        int ret;
        while ((ret = avcodec_receive_packet(enc.get(), packet.get())) >= 0) {
            check(avcodec_send_packet(dec.get(), packet.get()));
            drainDecoder(); av_packet_unref(packet.get());
        }
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) check(ret);
        return ret;
    };
    require(enc->frame_size == 1024, "unexpected native AAC frame size");
    for (size_t pos = 0; pos < input[0].size(); pos += enc->frame_size) {
        const int count = std::min<size_t>(enc->frame_size, input[0].size() - pos);
        auto f = frame(count, pos);
        for (int ch = 0; ch < 2; ++ch)
            std::copy_n(input[ch].data() + pos, count, reinterpret_cast<float *>(f->extended_data[ch]));
        check(avcodec_send_frame(enc.get(), f.get())); drainEncoder();
    }
    check(avcodec_send_frame(enc.get(), nullptr));
    require(drainEncoder() == AVERROR_EOF, "encoder did not drain");
    check(avcodec_send_packet(dec.get(), nullptr));
    require(drainDecoder() == AVERROR_EOF, "decoder did not drain");
    // Include codec priming and padding in peak measurement (do not hide transients).
    require(output[0].size() >= input[0].size(), "truncated decoded audio");
    return output;
}
double peak(const Audio &audio) {
    double value = 0;
    for (const auto &channel : audio) for (float sample : channel) {
        require(std::isfinite(sample), "non-finite sample");
        value = std::max(value, double(std::abs(sample)));
    }
    return value;
}
double rms(const Audio &audio) {
    double sum = 0;
    for (const auto &channel : audio) for (float sample : channel) sum += double(sample) * sample;
    return std::sqrt(sum / (2 * audio[0].size()));
}
double reconstructedPeak(const Audio &audio) {
    // 8x bandlimited reconstruction estimate, not a certified BS.1770 meter.
    AVChannelLayout layout = AV_CHANNEL_LAYOUT_STEREO;
    SwrContext *raw = nullptr;
    check(swr_alloc_set_opts2(&raw, &layout, AV_SAMPLE_FMT_FLTP, rate * 8,
                             &layout, AV_SAMPLE_FMT_FLTP, rate, 0, nullptr));
    std::unique_ptr<SwrContext, SwrDelete> swr(raw); check(swr_init(swr.get()));
    const int capacity = swr_get_out_samples(swr.get(), int(audio[0].size()));
    Audio upsampled;
    for (auto &channel : upsampled) channel.resize(capacity);
    uint8_t *out[] = {reinterpret_cast<uint8_t *>(upsampled[0].data()), reinterpret_cast<uint8_t *>(upsampled[1].data())};
    const uint8_t *in[] = {reinterpret_cast<const uint8_t *>(audio[0].data()), reinterpret_cast<const uint8_t *>(audio[1].data())};
    int count = swr_convert(swr.get(), out, capacity, in, int(audio[0].size())); check(count);
    for (auto &channel : upsampled) channel.resize(count);
    double value = peak(upsampled);
    // Flush reconstruction filter tails as well.
    Audio tail; for (auto &channel : tail) channel.resize(4096);
    uint8_t *tailOut[] = {reinterpret_cast<uint8_t *>(tail[0].data()), reinterpret_cast<uint8_t *>(tail[1].data())};
    while ((count = swr_convert(swr.get(), tailOut, 4096, nullptr, 0)) > 0) {
        for (const auto &channel : tail) for (int i = 0; i < count; ++i) {
            require(std::isfinite(channel[i]), "non-finite reconstruction");
            value = std::max(value, double(std::abs(channel[i])));
        }
    }
    check(count); return value;
}
Audio loudMix(int scenario) {
    AudioTimelineMixer mixer(0);
    Audio result;
    // Not an AAC frame multiple: exercises short final frame and codec flush.
    constexpr int duration = 3 * rate + 173;
    for (int pos = 0; pos < duration; pos += 480) {
        int count = std::min(480, duration - pos);
        for (unsigned source = 0; source < 2; ++source) {
            auto f = frame(count, pos);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < count; ++i) {
                const int n = pos + i;
                const double phase = 2 * pi * (ch ? 997 : 440) * n / rate;
                double value = 0;
                switch (scenario) {
                case 0: value = std::sin(phase); break; // coherent near-full-scale tones
                case 1: value = std::sin(phase) >= 0 ? 1 : -1; break; // hard square edges
                case 2: // abrupt bursts, isolated impulses, and silence/release/re-attack
                    value = n % 12000 < 2400 ? std::sin(phase) : 0;
                    if (n % 997 == 0) value = ch ? -1 : 1;
                    break;
                case 3: // high-frequency phase offsets and differing sources/channels
                    value = std::sin(2 * pi * (source ? 11003 : 10000) * n / rate + ch * .7);
                    break;
                }
                reinterpret_cast<float *>(f->extended_data[ch])[i] = float(.99 * value);
            }
            mixer.push(source, *f);
        }
        auto mixed = mixer.render(pos + count);
        require(mixed && mixed->pts == pos && mixed->nb_samples == count, "limiter shifted timeline");
        append(result, *mixed);
    }
    require(mixer.stats().limited > 0, "test did not exercise limiter");
    return result;
}
}
int main() {
    try {
        // Keep this regression independent of optional diagnostic I/O.
#ifdef _WIN32
        _putenv_s("SUBREC_AUDIO_MIX_DUMP_DIR", "");
#else
        unsetenv("SUBREC_AUDIO_MIX_DUMP_DIR");
#endif
        bool passed = true;
        for (int scenario = 0; scenario < 4; ++scenario) {
            auto mixed = loudMix(scenario);
            auto decoded = roundTrip(mixed);
            const double samplePeak = peak(decoded), truePeak = reconstructedPeak(decoded);
            const double level = rms(decoded);
            std::cout << "scenario=" << scenario << " PCM_peak=" << peak(mixed)
                      << " PCM_8x_peak=" << reconstructedPeak(mixed)
                      << " AAC_peak=" << samplePeak << " (" << 20 * std::log10(samplePeak)
                      << " dBFS) AAC_8x_peak=" << truePeak << " RMS=" << level << '\n';
            passed &= samplePeak < 1.0 && truePeak < 1.0 && level > .01;
        }
        require(passed, "AAC decoded peak / 8x reconstructed peak must be < 1.0 and RMS > 0.01");
        std::cout << "PASS: production mix -> native AAC 65536 bps -> decode, sample and 8x peaks\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

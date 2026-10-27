#include "filter/audio_timeline_mixer.h"
#include "encoder/audio_encoding_config.h"
#include "wav_pcm_reader.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
}
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <limits>
#include <string>
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
    // Match FFAEncoder::initAudio, including the shared production bitrate.
    enc->bit_rate = audio_encoding::kAacBitRate; enc->sample_rate = rate;
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
    constexpr int block = 4096, capacity = block*8 + 1024;
    Audio upsampled;
    for (auto &channel : upsampled) channel.resize(capacity);
    uint8_t *out[] = {reinterpret_cast<uint8_t *>(upsampled[0].data()), reinterpret_cast<uint8_t *>(upsampled[1].data())};
    double value = 0;
    auto measure = [&](int count) {
        check(count);
        for (const auto &channel : upsampled) for (int i = 0; i < count; ++i) {
            require(std::isfinite(channel[i]), "non-finite reconstruction");
            value = std::max(value, double(std::abs(channel[i])));
        }
    };
    for (size_t pos = 0; pos < audio[0].size(); pos += block) {
        const int count = std::min<size_t>(block, audio[0].size()-pos);
        const uint8_t *in[] = {reinterpret_cast<const uint8_t *>(audio[0].data()+pos),
                              reinterpret_cast<const uint8_t *>(audio[1].data()+pos)};
        measure(swr_convert(swr.get(), out, capacity, in, count));
    }
    // Include reconstruction filter tails; bounded buffers also allow long WAVs.
    int count;
    do {
        count = swr_convert(swr.get(), out, capacity, nullptr, 0);
        measure(count);
    } while (count > 0);
    return value;
}
Audio mix(const std::array<Audio, 2> &inputs, bool requireLimiting = false) {
    AudioTimelineMixer mixer(0);
    Audio result;
    const size_t duration = std::max(inputs[0][0].size(), inputs[1][0].size());
    require(duration > 0, "empty mix inputs");
    for (size_t pos = 0; pos < duration; pos += 480) {
        const int count = std::min<size_t>(480, duration-pos);
        for (unsigned source = 0; source < 2; ++source) {
            auto f = frame(count, pos);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < count; ++i)
                reinterpret_cast<float *>(f->extended_data[ch])[i] =
                    pos+i < inputs[source][ch].size() ? inputs[source][ch][pos+i] : 0;
            mixer.push(source, *f);
        }
        // Match the worker's 200 ms resampling/arrival margin.
        while (auto mixed = mixer.render(int64_t(pos) + count - 9600)) {
            require(mixed->pts == int64_t(result[0].size()), "mixer shifted timeline");
            append(result, *mixed);
        }
    }
    mixer.finish(0); mixer.finish(1);
    while (auto mixed = mixer.render(mixer.end())) {
        require(mixed->pts == int64_t(result[0].size()), "mixer shifted drained timeline");
        append(result, *mixed);
    }
    require(result[0].size() == duration, "resampler lost final samples");
    if (requireLimiting) require(mixer.stats().limited > 0, "test did not exercise limiter");
    return result;
}
Audio loudMix(int scenario) {
    // Not an AAC frame multiple: exercises short final frame and codec flush.
    constexpr int duration = 3 * rate + 173;
    std::array<Audio, 2> inputs;
    for (unsigned source = 0; source < 2; ++source) for (int ch = 0; ch < 2; ++ch) {
        auto &samples = inputs[source][ch]; samples.resize(duration);
        for (int n = 0; n < duration; ++n) {
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
            samples[n] = float(.99 * value);
        }
    }
    return mix(inputs, true);
}
Audio music(unsigned source) {
    Audio result;
    uint32_t noiseState = 0x12345678u + source;
    constexpr int duration = 8 * rate + 173;
    for (int ch = 0; ch < 2; ++ch) {
        result[ch].resize(duration);
        for (int n = 0; n < duration; ++n) {
            const double t = double(n)/rate;
            const double noteTime = std::fmod(t + source*.19, .75);
            const double envelope = (1-std::exp(-noteTime/.004))*std::exp(-noteTime/.22);
            const double frequency = (source ? 173.0 : 110.0) *
                std::pow(2.0, (int(t/.75)%4)*2.0/12.0);
            double harmonics = 0;
            for (int h = 1; h <= 6; ++h)
                harmonics += std::sin(2*pi*frequency*h*t + .31*h + ch*.2)/h;
            noiseState = noiseState*1664525u + 1013904223u;
            const double noise = double(noiseState)/2147483648.0 - 1;
            result[ch][n] = float(envelope*(harmonics + .12*noise));
        }
    }
    const double gain = .99/peak(result);
    for (auto &channel : result) for (auto &sample : channel) sample *= gain;
    const double crest = 20*std::log10(peak(result)/rms(result));
    std::cout << "music source=" << source << " peak=" << peak(result)
              << " RMS=" << rms(result) << " crest_dB=" << crest << '\n';
    require(crest >= 12 && crest <= 15, "synthetic music crest factor must be 12..15 dB");
    return result;
}
// Native AAC can overshoot strongly on hard synthetic edges (observed square
// peak about +4.3 dBFS). Allow that known codec behavior, but fail above +6 dBFS
// to catch excessive regressions. This is not a clipping-safety limit for music.
constexpr double kExtremeDecodedSamplePeakLimitDbfs = 6.0;
const double kExtremeDecodedSamplePeakLimit =
    std::pow(10.0, kExtremeDecodedSamplePeakLimitDbfs / 20.0);
bool extremeDecodedSamplePeakOk(double samplePeak) {
    return std::isfinite(samplePeak) && samplePeak <= kExtremeDecodedSamplePeakLimit;
}
void testExtremeDecodedSamplePeakLimit() {
    require(extremeDecodedSamplePeakOk(std::pow(10.0, 4.3/20.0)), "known AAC square overshoot must pass");
    require(extremeDecodedSamplePeakOk(std::nextafter(kExtremeDecodedSamplePeakLimit, 0.0)),
            "peak just below +6 dBFS must pass");
    require(extremeDecodedSamplePeakOk(kExtremeDecodedSamplePeakLimit), "peak exactly +6 dBFS must pass");
    require(!extremeDecodedSamplePeakOk(std::nextafter(kExtremeDecodedSamplePeakLimit,
                                                     std::numeric_limits<double>::infinity())),
            "peak just above +6 dBFS must fail");
    require(!extremeDecodedSamplePeakOk(2.0), "2x full scale exceeds +6 dBFS and must fail");
    require(!extremeDecodedSamplePeakOk(std::numeric_limits<double>::infinity()), "infinite peak must fail");
    require(!extremeDecodedSamplePeakOk(std::numeric_limits<double>::quiet_NaN()), "NaN peak must fail");
    std::cout << "PASS: extreme decoded sample peak boundary: <= +"
              << kExtremeDecodedSamplePeakLimitDbfs << " dBFS ("
              << kExtremeDecodedSamplePeakLimit << ") passes; above fails\n";
}
enum class DecodedPeakPolicy { Strict, ExtremeSynthetic };
bool verify(const char *name, const Audio &mixed, double minimumRms,
            DecodedPeakPolicy policy = DecodedPeakPolicy::Strict) {
    const auto decoded = roundTrip(mixed);
    // peak() checks every decoded sample for finiteness, including priming/padding.
    const double samplePeak = peak(decoded), truePeak = reconstructedPeak(decoded), level = rms(decoded);
    const double pcmPeak = peak(mixed);
    const double maximumSamplePeak = std::pow(10.0, -.5/20);
    const bool strict = policy == DecodedPeakPolicy::Strict;
    std::cout << "scenario=" << name << " PCM_peak=" << pcmPeak
              << " PCM_8x_peak=" << reconstructedPeak(mixed)
              << " AAC_peak=" << samplePeak << " (" << 20*std::log10(samplePeak)
              << " dBFS) AAC_8x_peak=" << truePeak << " RMS=" << level
              << " RMS_min=" << minimumRms
              << " decoded_peaks=" << (strict ? "strict" : "sample <= +6 dBFS; 8x report-only") << '\n';
    if (!strict) {
        std::cout << "INFO: " << name << " native AAC may overshoot on extreme synthetic signals;"
                  << " float decoding retains peaks above 1.0. Assert decoded sample peak <= +"
                  << kExtremeDecodedSamplePeakLimitDbfs
                  << " dBFS, PCM ceiling, RMS floor and finite samples; 8x peak remains report-only.\n";
    }
    const bool levelOk = std::isfinite(level) && level >= minimumRms && level > 0;
    const bool pcmOk = strict || pcmPeak <= AudioTimelineMixer::limiterCeiling + 1e-6;
    const bool decodedPeaksOk = strict ? (samplePeak < maximumSamplePeak && truePeak < 1.0)
                                       : extremeDecodedSamplePeakOk(samplePeak);
    const bool passed = levelOk && pcmOk && decodedPeaksOk;
    if (!passed) {
        std::cerr << "FAIL: " << name << ": finite RMS >= floor=" << levelOk;
        if (strict) std::cerr << ", sample peak < -0.5 dBFS=" << (samplePeak < maximumSamplePeak)
                              << ", 8x peak < 1=" << (truePeak < 1.0);
        else std::cerr << ", PCM peak <= ceiling + 1e-6=" << pcmOk
                       << ", decoded sample peak <= +" << kExtremeDecodedSamplePeakLimitDbfs
                       << " dBFS=" << decodedPeaksOk;
        std::cerr << '\n';
    }
    return passed;
}
std::filesystem::path wavEnvironment(unsigned source) {
#ifdef _WIN32
    const auto *value = _wgetenv(source ? L"SUBREC_AUDIO_MIX_AAC_MIC" : L"SUBREC_AUDIO_MIX_AAC_SYSTEM");
#else
    const auto *value = std::getenv(source ? "SUBREC_AUDIO_MIX_AAC_MIC" : "SUBREC_AUDIO_MIX_AAC_SYSTEM");
#endif
    return value ? std::filesystem::path(value) : std::filesystem::path{};
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
        std::cout << std::setprecision(9);
        std::cout << "native AAC bitrate=" << audio_encoding::kAacBitRate << " bps\n";
        testExtremeDecodedSamplePeakLimit();
        bool passed = true;
        // Measured with native FFmpeg n8.0.3, production bitrate/limiter, including
        // priming and padding. Fixed baselines prevent a quieter implementation
        // from lowering its own regression threshold. Floors are 1.5 dB below.
        constexpr double measuredRms[] = {.502132383, .676871251, .224678646, .357992407};
        const double rmsMargin = std::pow(10.0, -1.5/20);
        const char *names[] = {"sine", "square", "transients", "high-frequency"};
        for (int scenario = 0; scenario < 4; ++scenario)
            passed &= verify(names[scenario], loudMix(scenario), measuredRms[scenario]*rmsMargin,
                             scenario == 0 ? DecodedPeakPolicy::Strict : DecodedPeakPolicy::ExtremeSynthetic);
        std::array<Audio, 2> materials{music(0), music(1)};
        constexpr double measuredMusicRms = .215276092;
        passed &= verify("synthetic-music", mix(materials), measuredMusicRms*rmsMargin);
        bool haveWav = false;
        for (unsigned source = 0; source < 2; ++source) {
            const auto path = wavEnvironment(source);
            if (path.empty()) {
                std::cout << "SKIP: " << (source ? "MIC" : "SYSTEM")
                          << " WAV path unset; using built-in music for this source\n";
            } else {
                materials[source] = audio_test::readWav(path);
                std::cout << "WAV source=" << source << " frames=" << materials[source][0].size()
                          << " peak=" << peak(materials[source]) << " RMS=" << rms(materials[source]) << '\n';
                haveWav = true;
            }
        }
        if (haveWav) {
            const auto mixed = mix(materials);
            // User-supplied material has no fixed reference level. Check codec RMS
            // loss against this fixture's pre-encode mix, allowing 1.5 dB and the
            // measured AAC priming/padding duration (important for short clips).
            const size_t decodedFrames = ((mixed[0].size()+1023)/1024 + 1)*1024;
            const double reference = rms(mixed)*std::sqrt(double(mixed[0].size())/decodedFrames);
            passed &= verify("real-wav", mixed, reference*rmsMargin);
        }
        require(passed, "AAC regression failed (all scenarios measured; see individual assertions above)");
        std::cout << "PASS: production mix -> native AAC " << audio_encoding::kAacBitRate
                  << " bps -> decode, RMS floors / finite samples; sine/music/WAV: sample peak < -0.5 dBFS"
                  << " and 8x peak < 1; extreme synthetic inputs: PCM ceiling, decoded sample peak <= +6 dBFS, 8x report-only\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

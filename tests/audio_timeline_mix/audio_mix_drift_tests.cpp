#include "filter/audio_timeline_mixer.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__)); } while (false)
namespace {
constexpr int64_t epoch = 123456000000;
constexpr double pi = 3.14159265358979323846;
AudioTimelineMixer::Frame packet(int rate, int count, int64_t inputSample, double ppm,
                                  int jitterUs, unsigned source) {
    AudioTimelineMixer::Frame f(av_frame_alloc()); CHECK(f);
    f->sample_rate = rate; f->format = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_default(&f->ch_layout, 2);
    f->nb_samples = count; f->time_base = {1,1000000};
    f->pts = epoch + std::llround(inputSample * 1000000.0 / (rate * (1 + ppm/1000000))) + jitterUs;
    CHECK(av_frame_get_buffer(f.get(), 0) >= 0);
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < count; ++i)
        reinterpret_cast<float *>(f->extended_data[ch])[i] = ch == int(source)
            ? .2f + .05f * std::sin(2*pi*(source ? 997 : 440)*(inputSample+i)/rate) : 0;
    return f;
}
void drift(double magnitude, int rate0, bool jitter) {
    AudioTimelineMixer mixer(epoch);
    const int rates[] = {rate0, 48000};
    const double ppm[] = {magnitude, -magnitude};
    double previousPpm[2]{}, maxStep = 0, maxJump = 0, maxError = 0;
    double finalError[2]{}, finalPpm[2]{}, finalMaxError[2]{};
    int lastChange[2]{-100, -100};
    float previous[2]{};
    bool havePrevious = false;
    int64_t rendered = 0;
    // Three minutes: without compensation +/-200 ppm accumulates +/-36 ms,
    // enough for several old resync/trim/silence discontinuities.
    constexpr int packets = 18000;
    for (int n = 0; n < packets; ++n) {
        int64_t availableUs = INT64_MAX;
        const int noise = jitter && n > 0 ? (n % 2 ? 3000 : -3000) : 0;
        for (unsigned source = 0; source < 2; ++source) {
            auto f = packet(rates[source], rates[source]/100, int64_t(n)*(rates[source]/100),
                            ppm[source], noise, source);
            mixer.push(source, *f);
            availableUs = std::min<int64_t>(availableUs, f->pts-epoch+10000-noise);
            const auto &s = mixer.stats();
            const double step = std::abs(s.compensationPpm[source] - previousPpm[source]);
            maxStep = std::max(maxStep, step);
            if (step > .001) {
                CHECK(n - lastChange[source] >= 100); // at most one update per source second
                lastChange[source] = n;
            }
            previousPpm[source] = s.compensationPpm[source];
            maxError = std::max(maxError, std::abs(s.phaseErrorUs[source]-noise));
            if (n >= packets-1000) {
                finalError[source] += s.phaseErrorUs[source]-noise;
                finalMaxError[source] = std::max(finalMaxError[source], std::abs(s.phaseErrorUs[source]-noise));
                finalPpm[source] += s.compensationPpm[source];
            }
        }
        // Match the production worker's 200 ms scheduling/resampler margin.
        const int64_t target = (availableUs-200000)*48000/1000000;
        while (auto out = mixer.render(target)) {
            CHECK(out->pts == rendered); rendered += out->nb_samples;
            for (int i = 0; i < out->nb_samples; ++i) {
                for (int ch = 0; ch < 2; ++ch) {
                    float value = reinterpret_cast<float *>(out->extended_data[ch])[i];
                    CHECK(std::isfinite(value));
                    CHECK(value > .14);
                    CHECK(value < .26);
                    if (havePrevious) maxJump = std::max(maxJump, double(std::abs(value-previous[ch])));
                    previous[ch] = value;
                }
                havePrevious = true;
            }
        }
    }
    for (unsigned source = 0; source < 2; ++source) {
        const auto &s = mixer.stats();
        CHECK(s.resyncs[source] == 0 && s.trimmed[source] == 0 && s.silence[source] == 0);
        finalError[source] /= 1000; finalPpm[source] /= 1000;
        // Positive hardware ppm requires fewer output samples (negative delta).
        const double expected = -ppm[source];
        std::cout << "drift=" << ppm[source] << " rate=" << rates[source] << " jitter=" << jitter
                  << " residual_us=" << finalError[source] << " max_residual_us=" << finalMaxError[source]
                  << " compensation_ppm=" << finalPpm[source] << '\n';
        CHECK(std::abs(finalError[source]) < 150 && finalMaxError[source] < 150);
        CHECK(std::abs(finalPpm[source] - expected) < 6);
    }
    CHECK(maxStep <= 50.000001);
    CHECK(maxError < 5000); // well below the hard-resync threshold
    CHECK(maxJump < .01); // > largest natural tone slope, far below a click to silence
    std::cout << "max_step_ppm=" << maxStep << " max_adjacent_sample_jump=" << maxJump << '\n';
}
void jitterAndDiscontinuity() {
    AudioTimelineMixer mixer(epoch);
    for (int n = 0; n < 100; ++n) {
        auto f = packet(48000, 480, n*480, 0, n ? (n%2 ? 9000 : -9000) : 0, 0);
        mixer.push(0, *f);
    }
    CHECK(mixer.stats().resyncs[0] == 0);
    // A genuine 30 ms gap must still use capture PTS, not hide missing media.
    auto f = packet(48000, 480, 48000 + 1440, 0, 0, 0);
    mixer.push(0, *f);
    CHECK(mixer.stats().resyncs[0] == 1);
    mixer.finish(0);
    while (mixer.render(mixer.end())) {}
    CHECK(mixer.stats().silence[0] >= 1400);
}
}
int main() {
    try {
#ifdef _WIN32
        _putenv_s("SUBREC_AUDIO_MIX_DUMP_DIR", "");
#else
        unsetenv("SUBREC_AUDIO_MIX_DUMP_DIR");
#endif
        jitterAndDiscontinuity();
        drift(50, 44100, false); drift(100, 48000, false); drift(200, 48000, false);
        drift(200, 48000, true);
        std::cout << "PASS: +/-50..200 ppm, continuous samples, bounded slew, jitter and hard resync\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

#include "filter/audio_timeline_mixer.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__)); } while (false)
namespace {
constexpr int64_t epoch = 123456000000;
AudioTimelineMixer::Frame pcm(int64_t offsetSamples, int samples, float left, float right,
                               int rate = 48000, int channels = 2) {
    AudioTimelineMixer::Frame f(av_frame_alloc()); CHECK(f);
    f->format = AV_SAMPLE_FMT_FLTP; f->sample_rate = rate;
    av_channel_layout_default(&f->ch_layout, channels);
    f->nb_samples = samples;
    // Exercise non-zero clock epoch and an input time base different from abuffer.
    f->pts = epoch + av_rescale_q(offsetSamples, {1,48000}, {1,1000000});
    f->time_base = {1,1000000}; CHECK(av_frame_get_buffer(f.get(), 0) >= 0);
    for (int ch = 0; ch < channels; ++ch) {
        auto *p = reinterpret_cast<float *>(f->extended_data[ch]);
        for (int i = 0; i < samples; ++i) p[i] = ch ? right : left;
    }
    return f;
}
float value(const AVFrame &f, int channel, int sample) {
    return reinterpret_cast<const float *>(f.extended_data[channel])[sample];
}
void alignmentAndSilence() {
    AudioTimelineMixer mixer(epoch);
    auto system = pcm(0, 4800, .2f, .2f);
    auto mic = pcm(960, 960, .4f, .4f); // starts 20 ms late, ends 40 ms in
    mixer.push(0, *system); mixer.push(1, *mic);
    mixer.finish(1); // An ended input must not end the other source.
    for (int64_t pts = 0; pts < 4800; pts += 480) {
        auto f = mixer.render(4800); CHECK(f && f->pts == pts);
        CHECK(f->sample_rate == 48000 && f->format == AV_SAMPLE_FMT_FLTP);
        CHECK(f->ch_layout.nb_channels == 2 && f->nb_samples == 480);
        for (int i = 0; i < f->nb_samples; ++i) {
            float expected = pts + i >= 960 && pts + i < 1920 ? .6f : .2f;
            CHECK(std::abs(value(*f, 0, i) - expected) < 1e-5);
            CHECK(std::abs(value(*f, 1, i) - expected) < 1e-5);
        }
    }
    CHECK(!mixer.render(4800));
    CHECK(mixer.stats().silence[1] == 3840);
    // A late packet cannot shift the already-rendered timeline or be replayed.
    auto late = pcm(0, 480, .9f, .9f); mixer.push(1, *late);
    auto idle = mixer.render(5280); CHECK(idle->pts == 4800);
    CHECK(value(*idle, 0, 0) == 0);
}
void limiter() {
    AudioTimelineMixer mixer(epoch);
    auto f = pcm(0, 4800, .8f, .4f);
    mixer.push(0, *f); mixer.push(1, *f);
    for (int i = 0; i < 10; ++i) {
        auto out = mixer.render(4800);
        for (int j = 0; j < out->nb_samples; ++j) {
            float l = value(*out,0,j), r = value(*out,1,j);
            CHECK(std::isfinite(l) && std::abs(l) <= .950001);
            CHECK(std::abs(l - 2*r) < 1e-6); // linked gain preserves stereo image
        }
    }
    CHECK(mixer.stats().limited == 4800);
}
void resamplingAndDrain() {
    AudioTimelineMixer mixer(epoch);
    auto mono = pcm(0, 44100, .25f, .25f, 44100, 1);
    mixer.push(1, *mono); mixer.finish(1);
    CHECK(mixer.end() == 48000);
    int samples = 0;
    while (auto out = mixer.render(mixer.end())) {
        samples += out->nb_samples;
        for (int i = 0; i < out->nb_samples; ++i) {
            CHECK(value(*out,0,i) > 0 && value(*out,0,i) < .3f);
            CHECK(std::abs(value(*out,0,i) - value(*out,1,i)) < 1e-6);
        }
    }
    CHECK(samples == 48000);
}
void packedMicrophoneAndOverlap() {
    AudioTimelineMixer mixer(epoch);
    auto f = pcm(0, 480, .0f, .0f, 48000, 1);
    av_frame_unref(f.get());
    f->format = AV_SAMPLE_FMT_S16; f->sample_rate = 48000; f->nb_samples = 480;
    f->time_base = {1,1000000}; f->pts = epoch; av_channel_layout_default(&f->ch_layout, 1);
    CHECK(av_frame_get_buffer(f.get(), 0) >= 0);
    for (int i = 0; i < 480; ++i) reinterpret_cast<int16_t *>(f->data[0])[i] = 8192;
    mixer.push(1, *f);
    auto out = mixer.render(480);
    CHECK(value(*out,0,0) > .1f && value(*out,0,0) < .3f);
    CHECK(std::abs(value(*out,0,0) - value(*out,1,0)) < 1e-6);
    // The second packet overlaps already emitted samples by 240 samples.
    f->pts = epoch + 5000; mixer.push(1, *f);
    out = mixer.render(960);
    CHECK(value(*out,0,0) > 0);
    CHECK(value(*out,0,239) > 0 && value(*out,0,240) == 0);
    CHECK(mixer.stats().trimmed[1] == 240);
}
void clockDriftAndPacketSizes() {
    AudioTimelineMixer mixer(epoch);
    // Each packet is 479 samples but capture timestamps advance 480 samples.
    // Missing samples must stay at their clock position, not accumulate skew.
    for (int packet = 0; packet < 500; ++packet) {
        auto f = pcm(packet * 480, 479, .2f, .2f);
        mixer.push(0, *f);
        auto out = mixer.render((packet+1)*480);
        CHECK(out->pts == packet*480);
        CHECK(std::abs(value(*out,0,478)-.2f) < 1e-6);
        CHECK(value(*out,0,479) == 0);
    }
    CHECK(mixer.position() == 240000);
    CHECK(mixer.stats().silence[0] == 500);
    CHECK(mixer.stats().frames[0] == 500);
}
}
int main() {
    try {
        alignmentAndSilence(); limiter(); resamplingAndDrain(); packedMicrophoneAndOverlap(); clockDriftAndPacketSizes();
        std::cout << "PASS: timestamp alignment, idle/EOF silence, drift, limiter and 44.1k mono conversion\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

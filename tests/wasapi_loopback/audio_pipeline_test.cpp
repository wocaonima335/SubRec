#include "capture/wasapi_audio_pipeline.h"
#include "capture/wasapi_silence_policy.h"
#include "capture/wasapi_drain.h"
#include <vector>
#include <iostream>
#include <cmath>
#include <stdexcept>
using namespace capture;
#define require(condition) do { if (!(condition)) throw std::runtime_error(std::string(#condition) + " line " + std::to_string(__LINE__)); } while (false)
struct Sink : wasapi::AudioFrameSink {
    int64_t samples = 0;
    int finished = 0;
    bool reject = false;
    bool allSilent = true;
    int64_t lastCaptureUs = AV_NOPTS_VALUE;
    int64_t lastCaptureEndUs = AV_NOPTS_VALUE;
    bool checkCaptureOrder = false;
    std::vector<float> left;
    bool push(AVFrame *frame) override {
        require(finished == 0); // Nothing may be delivered after terminal EOF.
        if (reject) return false;
        require(frame->format == AV_SAMPLE_FMT_FLTP && frame->sample_rate == 48000);
        require(frame->ch_layout.nb_channels == 2 && frame->pts == samples);
        require(frame->duration == frame->nb_samples);
        for (int channel = 0; channel < 2; ++channel) {
            const auto *values = reinterpret_cast<const float *>(frame->extended_data[channel]);
            for (int i = 0; i < frame->nb_samples; ++i) {
                require(std::isfinite(values[i]));
                if (std::abs(values[i]) > 0.00001f) allSilent = false;
            }
        }
        if (const auto *entry = av_dict_get(frame->metadata, "capture_time_us", nullptr, 0)) {
            lastCaptureUs = std::stoll(entry->value);
            if (checkCaptureOrder && lastCaptureEndUs != AV_NOPTS_VALUE)
                require(lastCaptureUs >= lastCaptureEndUs - 1); // microsecond rounding only
            lastCaptureEndUs = lastCaptureUs + av_rescale_q(frame->nb_samples,
                {1,wasapi::kOutputSampleRate}, {1,1000000});
        } else require(!checkCaptureOrder);
        const auto *values = reinterpret_cast<const float *>(frame->extended_data[0]);
        left.insert(left.end(), values, values + frame->nb_samples);
        samples += frame->nb_samples;
        av_frame_unref(frame);
        return true;
    }
    void finish() override { ++finished; }
};
void conversion(int rate, bool surround) {
    Sink sink; wasapi::AudioPipeline pipeline(sink);
    AVChannelLayout layout = AV_CHANNEL_LAYOUT_STEREO;
    if (surround) layout = AV_CHANNEL_LAYOUT_5POINT1;
    pipeline.configure(media::AudioFormat(rate, AV_SAMPLE_FMT_FLT, layout, {1,rate}));
    for (int i = 0; i < 100; ++i) {
        auto frame = media::makeFrame();
        frame->format = AV_SAMPLE_FMT_FLT; frame->sample_rate = rate;
        frame->ch_layout = layout; frame->nb_samples = rate / 100; frame->pts = i * frame->nb_samples;
        require(av_frame_get_buffer(frame.get(), 0) >= 0);
        auto *values = reinterpret_cast<float *>(frame->data[0]);
        for (int j = 0; j < frame->nb_samples * layout.nb_channels; ++j) values[j] = 0.25f;
        require(pipeline.packet(*frame));
    }
    if (rate == 44100) require(sink.samples < 48000); // swresample has a tail to drain
    pipeline.stop(false); pipeline.stop(false);
    require(sink.samples == 48000 && sink.finished == 1 && !sink.allSilent);
}
namespace {
constexpr int64_t epoch = 123456000000;
constexpr int64_t rate = wasapi::kOutputSampleRate;
constexpr int64_t margin = wasapi::SilencePolicy::kSafetyUs * rate / 1000000;
int64_t atMs(int ms) { return int64_t(ms)*rate/1000; }
media::FramePtr pcm(int inputRate, int count, float value = .25f) {
    auto f = media::makeFrame();
    f->format = AV_SAMPLE_FMT_FLT; f->sample_rate = inputRate;
    f->ch_layout = AV_CHANNEL_LAYOUT_STEREO; f->nb_samples = count; f->pts = AV_NOPTS_VALUE;
    require(av_frame_get_buffer(f.get(), 0) >= 0);
    auto *data = reinterpret_cast<float *>(f->data[0]);
    for (int i = 0; i < count*2; ++i) data[i] = value;
    return f;
}
void configure(wasapi::AudioPipeline &pipeline, int inputRate = rate) {
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    pipeline.configure(media::AudioFormat(inputRate, AV_SAMPLE_FMT_FLT, stereo, {1,inputRate}));
    pipeline.setSessionStartUs(epoch);
}
void poll(wasapi::AudioPipeline &pipeline, wasapi::SilencePolicy &policy, int ms) {
    const auto target = policy.target(epoch+ms*1000, pipeline.samples(), pipeline.pendingSamples(), false, true);
    require(pipeline.idleUntil(target));
}
}
void neverReceivedAndStop() {
    Sink sink; sink.checkCaptureOrder = true;
    wasapi::AudioPipeline pipeline(sink); configure(pipeline);
    wasapi::SilencePolicy policy(epoch);
    for (int ms = 0; ms <= 1000; ms += 5) poll(pipeline, policy, ms);
    require(sink.samples == rate-margin && sink.allSilent);
    require(pipeline.generatedSilenceSamples() == sink.samples);
    require(sink.lastCaptureEndUs == epoch+900000);
    const auto target = policy.target(epoch+1050000, pipeline.samples(), pipeline.pendingSamples(), false, true);
    const auto tail = pipeline.stop(false, target, epoch);
    require(tail == atMs(50) && sink.samples == atMs(950));
    require(sink.lastCaptureEndUs == epoch+950000 && sink.finished == 1);
    require(pipeline.generatedSilenceSamples() == sink.samples);
    require(pipeline.stop(false, target+1000, epoch) == 0 && sink.finished == 1);
    std::cout << "pure idle: 1 s -> " << rate-margin << " samples; stop at 1.05 s -> "
              << sink.samples << ", tail=" << tail << ", generated=" << pipeline.generatedSilenceSamples() << '\n';
}
void guardedSilenceAndResume() {
    // Reserve 44.1k resampler tail, then flush it BEFORE silence, never after it.
    Sink sink; sink.checkCaptureOrder = true;
    wasapi::AudioPipeline pipeline(sink); configure(pipeline, 44100);
    auto first = pcm(44100, 441);
    require(pipeline.packet(*first, epoch));
    require(pipeline.pendingSamples() > 0 && sink.samples < atMs(10));
    wasapi::SilencePolicy policy(epoch); policy.packet();
    const auto initial = sink.samples;
    for (int ms = 10; ms <= 70; ms += 5) {
        poll(pipeline, policy, ms); require(sink.samples == initial);
    }
    for (int ms = 75; ms <= 1000; ms += 5) poll(pipeline, policy, ms);
    require(sink.samples == rate-margin && pipeline.pendingSamples() == 0);
    for (int i = 0; i < atMs(10); ++i) require(std::abs(sink.left[i]-.25f) < .001f);
    for (int64_t i = atMs(10); i < sink.samples; ++i) require(sink.left[i] == 0);
    require(pipeline.generatedSilenceSamples() == sink.samples-atMs(10));
    // A packet at 1 s proves that the safety margin [0.9,1.0) was also a gap.
    policy.packet(); auto resumed = pcm(44100, 441, .5f);
    require(pipeline.packet(*resumed, epoch+1000000));
    pipeline.stop(false);
    require(sink.samples == atMs(1010) && sink.finished == 1);
    for (int64_t i = atMs(10); i < rate; ++i) require(sink.left[i] == 0);
    for (int64_t i = rate; i < sink.samples; ++i) require(std::abs(sink.left[i]-.5f) < .001f);
    require(pipeline.generatedSilenceSamples() == rate-atMs(10));
    std::cout << "44.1k resume: samples=" << sink.samples << " generated="
              << pipeline.generatedSilenceSamples() << " real=" << atMs(20) << '\n';
}
void lateResume() {
    Sink sink; sink.checkCaptureOrder = true;
    wasapi::AudioPipeline pipeline(sink); configure(pipeline);
    wasapi::SilencePolicy policy(epoch);
    for (int ms = 0; ms <= 1000; ms += 5) poll(pipeline, policy, ms);
    const auto silent = sink.samples;
    auto old = pcm(rate, atMs(10), -.75f); policy.packet();
    require(pipeline.packet(*old, epoch+800000)); // entirely behind padded cursor
    require(sink.samples == silent);
    auto overlap = pcm(rate, atMs(20), .5f);
    require(pipeline.packet(*overlap, epoch+895000)); // discard [895,900), retain [900,915)
    require(sink.samples == atMs(915));
    auto next = pcm(rate, atMs(10), .25f);
    require(pipeline.packet(*next, epoch+915000));
    pipeline.stop(false);
    require(sink.samples == atMs(925) && pipeline.generatedSilenceSamples() == silent);
    require(pipeline.trimmedSamples() == static_cast<uint64_t>(atMs(15)));
    for (int64_t i = 0; i < silent; ++i) require(sink.left[i] == 0);
    for (int64_t i = silent; i < atMs(915); ++i) require(sink.left[i] == .5f);
    for (int64_t i = atMs(915); i < sink.samples; ++i) require(sink.left[i] == .25f);
    std::cout << "late resume: discarded 480+240 overlapping samples, output=" << sink.samples << '\n';
}
void delayedDataDoesNotPad() {
    for (int delay : {20,40,60}) {
        Sink sink; wasapi::AudioPipeline pipeline(sink); configure(pipeline);
        wasapi::SilencePolicy policy(epoch);
        auto first = pcm(rate, atMs(10)); require(pipeline.packet(*first, epoch)); policy.packet();
        for (int ms = 10; ms <= 10+delay; ms += 5) poll(pipeline, policy, ms);
        auto late = pcm(rate, atMs(10)); require(pipeline.packet(*late, epoch+10000)); policy.packet();
        require(pipeline.generatedSilenceSamples() == 0 && sink.samples == atMs(20));
        pipeline.stop(false); require(sink.finished == 1);
    }
}
void delayedDeviceStart() {
    Sink sink; sink.checkCaptureOrder = true;
    wasapi::AudioPipeline pipeline(sink); configure(pipeline);
    wasapi::SilencePolicy policy(epoch);
    // This is a device packet starting at +50 ms, not a packet timestamped at
    // zero and merely arriving late. Its timestamp establishes the startup gap.
    auto first = pcm(rate, atMs(10));
    require(pipeline.packet(*first, epoch+50000)); policy.packet();
    require(sink.samples == atMs(60) && pipeline.generatedSilenceSamples() == atMs(50));
    for (int ms = 60; ms <= 1000; ms += 5) poll(pipeline, policy, ms);
    pipeline.stop(false);
    require(sink.samples == rate-margin && pipeline.generatedSilenceSamples() == sink.samples-atMs(10));
    for (int i = 0; i < sink.samples; ++i)
        require(sink.left[i] == (i >= atMs(50) && i < atMs(60) ? .25f : 0));
}
void repeatedIdleAndResume() {
    Sink sink; sink.checkCaptureOrder = true;
    wasapi::AudioPipeline pipeline(sink); configure(pipeline, 44100);
    wasapi::SilencePolicy policy(epoch);
    // Multiple flush/recreate cycles must not accumulate duplicated converter tails.
    for (int cycle = 0; cycle < 3; ++cycle) {
        auto first = pcm(44100, 441, .125f*(cycle+1));
        auto second = pcm(44100, 441, .125f*(cycle+1));
        require(pipeline.packet(*first, epoch+cycle*1000000)); policy.packet();
        require(pipeline.packet(*second, epoch+cycle*1000000+10000)); policy.packet();
        for (int ms = cycle*1000+20; ms <= (cycle+1)*1000; ms += 5) poll(pipeline, policy, ms);
        require(sink.samples == (cycle+1)*rate-margin);
        require(pipeline.generatedSilenceSamples() == sink.samples-(cycle+1)*atMs(20));
    }
    pipeline.stop(false);
    require(sink.finished == 1 && pipeline.pendingSamples() == 0);
    std::cout << "3 idle/resume cycles: samples=" << sink.samples
              << " generated=" << pipeline.generatedSilenceSamples() << '\n';
}
void initialConfigureWithoutSession() {
    Sink sink; sink.checkCaptureOrder = true;
    wasapi::AudioPipeline pipeline(sink);
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    pipeline.configure(media::AudioFormat(rate, AV_SAMPLE_FMT_FLT, stereo, {1,int(rate)}));
    auto first = pcm(rate, atMs(10));
    require(pipeline.packet(*first, epoch+50000));
    pipeline.stop(false);
    // Without a session origin, configure must not infer a startup gap.
    require(sink.samples == atMs(10) && pipeline.generatedSilenceSamples() == 0);
    require(sink.lastCaptureUs == epoch+50000 && sink.finished == 1);
    for (float value : sink.left) require(value == .25f);
}
void reconnectImmediatePacket() {
    for (int oldRate : {48000, 44100}) {
        Sink sink; sink.checkCaptureOrder = true;
        wasapi::AudioPipeline pipeline(sink); configure(pipeline, oldRate);
        auto first = pcm(oldRate, oldRate/100);
        require(pipeline.packet(*first, epoch));
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        // Reopen without resetting the session or entering idleUntil(). The
        // old converter's real tail must be drained before the new gap is filled.
        pipeline.configure(media::AudioFormat(rate, AV_SAMPLE_FMT_FLT, stereo, {1,int(rate)}));
        require(sink.samples == atMs(10) && pipeline.generatedSilenceSamples() == 0);
        auto resumed = pcm(rate, atMs(10), .5f);
        require(pipeline.packet(*resumed, epoch+30000));
        auto next = pcm(rate, atMs(10), .75f);
        require(pipeline.packet(*next, epoch+40000));
        pipeline.stop(false);
        require(sink.samples == atMs(50) && sink.finished == 1);
        require(pipeline.generatedSilenceSamples() == atMs(20) && pipeline.trimmedSamples() == 0);
        require(sink.lastCaptureEndUs == epoch+50000);
        for (int64_t i = 0; i < sink.samples; ++i) {
            const float expected = i < atMs(10) ? .25f : i < atMs(30) ? 0 :
                                   i < atMs(40) ? .5f : .75f;
            require(std::abs(sink.left[i]-expected) < .001f);
        }
        std::cout << "reconnect " << oldRate << " -> " << rate << ": samples=" << sink.samples
                  << " generated=" << pipeline.generatedSilenceSamples() << '\n';
    }
}
void reconnectOverlappingPackets() {
    Sink sink; sink.checkCaptureOrder = true;
    wasapi::AudioPipeline pipeline(sink); configure(pipeline);
    auto first = pcm(rate, atMs(10)); require(pipeline.packet(*first, epoch));
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    pipeline.configure(media::AudioFormat(rate, AV_SAMPLE_FMT_FLT, stereo, {1,int(rate)}));
    auto stale = pcm(rate, atMs(5), -.75f);
    require(pipeline.packet(*stale, epoch)); // discard entire [0,5) ms packet
    require(sink.samples == atMs(10));
    auto overlap = pcm(rate, atMs(10), .5f);
    require(pipeline.packet(*overlap, epoch+5000)); // keep only [10,15) ms
    require(sink.samples == atMs(15));
    auto next = pcm(rate, atMs(10), .75f);
    require(pipeline.packet(*next, epoch+15000));
    pipeline.stop(false);
    require(sink.samples == atMs(25) && pipeline.generatedSilenceSamples() == 0);
    require(pipeline.trimmedSamples() == static_cast<uint64_t>(atMs(10)));
    require(sink.lastCaptureEndUs == epoch+25000 && sink.finished == 1);
    for (int64_t i = 0; i < sink.samples; ++i)
        require(sink.left[i] == (i < atMs(10) ? .25f : i < atMs(15) ? .5f : .75f));
    std::cout << "reconnect overlap: discarded " << atMs(10)
              << " samples, output=" << sink.samples << '\n';
}
void terminalDrainAndPadding() {
    // Same final drain helper as production; Stop must not manufacture a new
    // idle interval. Test both unqualified and already qualified tail padding.
    for (int idleMs : {60, 100, 200}) {
        Sink sink; sink.checkCaptureOrder = true;
        wasapi::AudioPipeline pipeline(sink); configure(pipeline, 44100);
        wasapi::SilencePolicy policy(epoch);
        int packets = 0;
        auto drained = wasapi::drainPackets([] { return false; }, [&] { return packets < 3; }, [&] {
            auto f = pcm(44100, 441);
            policy.packet();
            return pipeline.packet(*f, epoch+(packets++)*10000);
        });
        require(drained.empty && drained.receivedPacket && packets == 3);
        require(sink.samples < atMs(30) && pipeline.pendingSamples() > 0);
        for (int ms = 30; ms < 30+idleMs; ms += 5)
            policy.target(epoch+ms*1000, pipeline.samples(), pipeline.pendingSamples(), false, true);
        const auto reserved = pipeline.pendingSamples();
        const auto target = policy.target(epoch+(30+idleMs)*1000, pipeline.samples(), reserved, false, true);
        const auto generated = pipeline.stop(false, target, epoch);
        const auto expected = idleMs < 100 ? atMs(30) : std::max(atMs(30), atMs(30+idleMs)-margin-reserved);
        require(sink.finished == 1 && sink.samples == expected);
        require(generated == expected-atMs(30) && pipeline.generatedSilenceSamples() == generated);
        for (int i = 0; i < atMs(30); ++i) require(std::abs(sink.left[i]-.25f) < .001f);
        for (int64_t i = atMs(30); i < sink.samples; ++i) require(sink.left[i] == 0);
        require(pipeline.stop(false, target+1000, epoch) == 0 && sink.finished == 1);
        std::cout << "stop idle_ms=" << idleMs << " samples=" << sink.samples
                  << " generated=" << generated << " reserved=" << reserved << '\n';
    }
    Sink aborted; wasapi::AudioPipeline pipeline(aborted);
    require(pipeline.stop(true, rate, epoch) == 0);
    require(aborted.samples == 0 && aborted.finished == 0 && pipeline.generatedSilenceSamples() == 0);
}

int main() {
    try {
        initialConfigureWithoutSession(); reconnectImmediatePacket(); reconnectOverlappingPackets();
        terminalDrainAndPadding();
        neverReceivedAndStop(); guardedSilenceAndResume(); lateResume(); delayedDataDoesNotPad();
        delayedDeviceStart(); repeatedIdleAndResume();
        conversion(48000, false); conversion(44100, false); conversion(48000, true);
        Sink sink; wasapi::AudioPipeline pipeline(sink);
        require(pipeline.silenceUntil(240001)); require(sink.samples == 240001 && sink.allSilent);
        require(pipeline.silenceUntil(200000)); require(sink.samples == 240001 && sink.allSilent);
        pipeline.stop(true); require(sink.finished == 0);
        Sink stamped; wasapi::AudioPipeline timestamped(stamped);
        timestamped.setCaptureTimeUs(123456000000);
        require(timestamped.silenceUntil(960));
        require(stamped.lastCaptureUs == 123456010000 && stamped.samples == 960);
        timestamped.stop(false); require(stamped.finished == 1);
        Sink closed; closed.reject = true; wasapi::AudioPipeline failed(closed);
        require(!failed.silenceUntil(480)); failed.stop(false); require(closed.finished == 0);
        require(failed.generatedSilenceSamples() == 0); // rejected frames never count as delivered silence
        std::cout << "WASAPI PCM conversion, PTS, idle silence, drain and abort passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}

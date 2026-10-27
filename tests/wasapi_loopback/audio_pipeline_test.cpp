#include "capture/wasapi_audio_pipeline.h"
#include "capture/wasapi_silence_policy.h"
#include "capture/wasapi_drain.h"
#include <vector>
#include <iostream>
#include <cmath>
#include <stdexcept>
using namespace capture;
void require(bool condition) { if (!condition) throw std::runtime_error("WASAPI pipeline assertion failed"); }
struct Sink : wasapi::AudioFrameSink {
    int64_t samples = 0;
    int finished = 0;
    bool reject = false;
    bool allSilent = true;
    int64_t lastCaptureUs = AV_NOPTS_VALUE;
    std::vector<float> left;
    bool push(AVFrame *frame) override {
        require(finished == 0); // Nothing may be delivered after terminal EOF.
        if (reject) return false;
        require(frame->format == AV_SAMPLE_FMT_FLTP && frame->sample_rate == 48000);
        require(frame->ch_layout.nb_channels == 2 && frame->pts == samples);
        for (int channel = 0; channel < 2; ++channel) {
            const auto *values = reinterpret_cast<const float *>(frame->extended_data[channel]);
            for (int i = 0; i < frame->nb_samples; ++i) {
                require(std::isfinite(values[i]));
                if (std::abs(values[i]) > 0.00001f) allSilent = false;
            }
        }
        if (const auto *entry = av_dict_get(frame->metadata, "capture_time_us", nullptr, 0))
            lastCaptureUs = std::stoll(entry->value);
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
void guardedSilence() {
    // Real resampler tail must be reserved by the same policy/call sequence as
    // LoopbackCapture. A later drain must not overshoot the last packet end.
    Sink sink; wasapi::AudioPipeline pipeline(sink);
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    pipeline.configure(media::AudioFormat(44100, AV_SAMPLE_FMT_FLT, stereo, {1,44100}));
    auto frame = media::makeFrame();
    frame->format = AV_SAMPLE_FMT_FLT; frame->sample_rate = 44100;
    frame->ch_layout = stereo; frame->nb_samples = 441;
    require(av_frame_get_buffer(frame.get(), 0) >= 0);
    require(av_samples_set_silence(frame->extended_data, 0, frame->nb_samples, 2, AV_SAMPLE_FMT_FLT) == 0);
    require(pipeline.packet(*frame, 123456100000)); // 100 ms gap before this packet
    require(pipeline.pendingSamples() > 0);
    wasapi::SilencePolicy policy;
    const int64_t end = wasapi::kOutputSampleRate / 10 + wasapi::kOutputFrameSamples;
    policy.packet(end);
    for (int ms = 0; ms < 100; ms += 5)
        require(policy.target(ms*1000, pipeline.samples(), pipeline.pendingSamples(), false, true) == pipeline.samples());
    const auto target = policy.target(100000, pipeline.samples(), pipeline.pendingSamples(), false, true);
    require(target > pipeline.samples() && target < end);
    pipeline.setCaptureTimeUs(123456000000 + av_rescale_q(pipeline.samples(),
                              {1,wasapi::kOutputSampleRate}, {1,1000000}));
    require(pipeline.silenceUntil(target));
    pipeline.stop(false);
    require(sink.samples <= end && end - sink.samples <= 1 && sink.allSilent && sink.finished == 1);
}
void terminalDrainAndPadding() {
    // The same final drain helper feeds actual 44.1k PCM into swresample.
    // Padding is allowed only if the empty interval qualified before Stop.
    for (int idleMs : {60, 100}) {
        Sink sink; wasapi::AudioPipeline pipeline(sink);
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        pipeline.configure(media::AudioFormat(44100, AV_SAMPLE_FMT_FLT, stereo, {1,44100}));
        wasapi::SilencePolicy policy;
        int packets = 0;
        auto drained = wasapi::drainPackets([] { return false; }, [&] { return packets < 3; }, [&] {
            auto frame = media::makeFrame();
            frame->format = AV_SAMPLE_FMT_FLT; frame->sample_rate = 44100;
            frame->ch_layout = stereo; frame->nb_samples = 441;
            require(av_frame_get_buffer(frame.get(), 0) >= 0);
            auto *data = reinterpret_cast<float *>(frame->data[0]);
            for (int i = 0; i < 441*2; ++i) data[i] = .25f;
            ++packets;
            policy.packet(wasapi::kOutputSampleRate / 10 + packets * wasapi::kOutputFrameSamples);
            return pipeline.packet(*frame, 123456100000 + (packets-1)*10000);
        });
        require(drained.empty && drained.receivedPacket && packets == 3);
        require(sink.samples < 3 * wasapi::kOutputFrameSamples && pipeline.pendingSamples() > 0);
        // Simulate final observation before client Stop, not an artificially
        // prolonged wait after it. A 60 ms gap still cannot authorize padding.
        for (int ms = 0; ms < idleMs; ms += 5)
            require(policy.target(ms*1000, pipeline.samples(), pipeline.pendingSamples(), false, true) == pipeline.samples());
        const auto target = policy.target(idleMs*1000, pipeline.samples(), 0, false, true);
        const auto generated = pipeline.stop(false, target, 123456000000);
        const auto realSamples = 3 * wasapi::kOutputFrameSamples;
        require(sink.finished == 1);
        require(sink.samples == realSamples + (idleMs == 100 ? wasapi::kOutputSampleRate / 10 : 0));
        require(generated == (idleMs == 100 ? wasapi::kOutputSampleRate / 10 : 0));
        for (int i = 0; i < realSamples; ++i) require(std::abs(sink.left[i] - .25f) < .001f);
        for (int64_t i = realSamples; i < sink.samples; ++i) require(sink.left[i] == 0);
        // Repeated finalization cannot append audio or emit a second EOF.
        require(pipeline.stop(false, target + 1000, 123456000000) == 0 && sink.finished == 1);
    }
    Sink aborted; wasapi::AudioPipeline pipeline(aborted);
    require(pipeline.stop(true, wasapi::kOutputSampleRate, 123456000000) == 0);
    require(aborted.samples == 0 && aborted.finished == 0);
}
int main() {
    try {
        terminalDrainAndPadding();
        guardedSilence();
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
        std::cout << "WASAPI PCM conversion, PTS, idle silence, drain and abort passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}

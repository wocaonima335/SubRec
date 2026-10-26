#include "capture/wasapi_audio_pipeline.h"
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
    bool push(AVFrame *frame) override {
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
int main() {
    try {
        conversion(48000, false); conversion(44100, false); conversion(48000, true);
        Sink sink; wasapi::AudioPipeline pipeline(sink);
        require(pipeline.silenceUntil(240001)); require(sink.samples == 240001 && sink.allSilent);
        require(pipeline.silenceUntil(200000)); require(sink.samples == 240001 && sink.allSilent);
        pipeline.stop(true); require(sink.finished == 0);
        Sink closed; closed.reject = true; wasapi::AudioPipeline failed(closed);
        require(!failed.silenceUntil(480)); failed.stop(false); require(closed.finished == 0);
        std::cout << "WASAPI PCM conversion, PTS, idle silence, drain and abort passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}

#pragma once
#include "wasapi_audio_sink.h"
#include "media/media_conversion.h"
#include <algorithm>

namespace capture::wasapi {
// Hardware-independent PCM packet seam. All methods run on the source thread.
class AudioPipeline {
public:
    explicit AudioPipeline(AudioFrameSink &sink) : sink_(sink) {}
    void configure(const media::AudioFormat &format) {
        drainConverter();
        converter_ = std::make_unique<media::AudioConverter>(format);
    }
    bool packet(const AVFrame &frame) { return deliver(converter_->convert(frame)); }
    int64_t samples() const { return samples_; }
    bool silenceUntil(int64_t target) {
        while (target > samples_ && !aborted_) {
            auto frame = media::makeFrame();
            frame->format = AV_SAMPLE_FMT_FLTP;
            frame->sample_rate = kOutputSampleRate;
            frame->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
            frame->nb_samples = static_cast<int>(std::min<int64_t>(480, target - samples_));
            if (av_frame_get_buffer(frame.get(), 0) < 0) throw std::bad_alloc();
            av_samples_set_silence(frame->extended_data, 0, frame->nb_samples, 2, AV_SAMPLE_FMT_FLTP);
            if (!deliver(std::move(frame))) return false;
        }
        return !aborted_;
    }
    void stop(bool abort) {
        if (abort) { aborted_ = true; return; }
        drainConverter();
        if (!aborted_ && !finished_) { finished_ = true; sink_.finish(); }
    }
private:
    bool deliver(media::FramePtr frame) {
        if (aborted_) return false;
        if (!frame) return true;
        frame->pts = samples_;
        frame->time_base = {1, kOutputSampleRate};
        const int count = frame->nb_samples;
        if (!sink_.push(frame.get())) { aborted_ = true; return false; }
        samples_ += count;
        return true;
    }
    void drainConverter() {
        if (converter_) while (!aborted_) {
            auto frame = converter_->drain();
            if (!frame) break;
            if (!deliver(std::move(frame))) break;
        }
    }
    AudioFrameSink &sink_;
    std::unique_ptr<media::AudioConverter> converter_;
    int64_t samples_ = 0;
    bool aborted_ = false, finished_ = false;
};
}

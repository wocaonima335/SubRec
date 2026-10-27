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
    void setCaptureTimeUs(int64_t time) { captureUs_ = time; }
    bool packet(const AVFrame &frame, int64_t captureUs = AV_NOPTS_VALUE) {
        captureUs_ = captureUs == AV_NOPTS_VALUE ? AV_NOPTS_VALUE : captureUs - converter_->delayUs();
        const bool ok = deliver(converter_->convert(frame));
        converterTailUs_ = captureUs_;
        return ok;
    }
    int64_t samples() const { return samples_; }
    int64_t pendingSamples() const {
        // Round up: even a fractional resampler tail must not become padding.
        return converter_ ? av_rescale_rnd(converter_->delayUs(), kOutputSampleRate,
                                           1000000, AV_ROUND_UP) : 0;
    }
    bool silenceUntil(int64_t target) {
        while (target > samples_ && !aborted_) {
            auto frame = media::makeFrame();
            frame->format = AV_SAMPLE_FMT_FLTP;
            frame->sample_rate = kOutputSampleRate;
            frame->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
            frame->nb_samples = static_cast<int>(std::min<int64_t>(kOutputFrameSamples, target - samples_));
            if (av_frame_get_buffer(frame.get(), 0) < 0) throw std::bad_alloc();
            av_samples_set_silence(frame->extended_data, 0, frame->nb_samples, 2, AV_SAMPLE_FMT_FLTP);
            if (!deliver(std::move(frame))) return false;
        }
        return !aborted_;
    }
    // Terminal flush precedes any policy-approved tail padding and EOF. Return
    // generated silence only (exclude real samples released by the resampler).
    int64_t stop(bool abort, int64_t silenceTarget = 0, int64_t sessionStartUs = AV_NOPTS_VALUE) {
        if (abort) { aborted_ = true; return 0; }
        if (finished_ || aborted_) return 0;
        drainConverter();
        const auto before = samples_;
        if (!aborted_ && silenceTarget > samples_) {
            captureUs_ = sessionStartUs == AV_NOPTS_VALUE ? AV_NOPTS_VALUE :
                sessionStartUs + av_rescale_q(samples_, {1,kOutputSampleRate}, {1,1000000});
            silenceUntil(silenceTarget);
        }
        if (!aborted_ && !finished_) { finished_ = true; sink_.finish(); }
        return samples_ - before;
    }
private:
    bool deliver(media::FramePtr frame) {
        if (aborted_) return false;
        if (!frame) return true;
        frame->pts = samples_;
        frame->time_base = {1, kOutputSampleRate};
        const int count = frame->nb_samples;
        if (captureUs_ != AV_NOPTS_VALUE) {
            const int result = av_dict_set_int(&frame->metadata, "capture_time_us", captureUs_, 0);
            if (result < 0) throw media::FfmpegError("capture timestamp", result);
            captureUs_ += av_rescale_q(count, {1,kOutputSampleRate}, {1,1000000});
        }
        if (!sink_.push(frame.get())) { aborted_ = true; return false; }
        samples_ += count;
        return true;
    }
    void drainConverter() {
        captureUs_ = converterTailUs_;
        if (converter_) while (!aborted_) {
            auto frame = converter_->drain();
            if (!frame) break;
            if (!deliver(std::move(frame))) break;
        }
    }
    AudioFrameSink &sink_;
    std::unique_ptr<media::AudioConverter> converter_;
    int64_t samples_ = 0;
    int64_t captureUs_ = AV_NOPTS_VALUE;
    int64_t converterTailUs_ = AV_NOPTS_VALUE;
    bool aborted_ = false, finished_ = false;
};
}

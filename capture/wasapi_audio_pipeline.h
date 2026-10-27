#pragma once
#include "wasapi_audio_sink.h"
#include "media/media_conversion.h"
#include <algorithm>
#include <cstring>

namespace capture::wasapi {
// Hardware-independent PCM packet seam. All methods run on the source thread.
class AudioPipeline {
public:
    explicit AudioPipeline(AudioFrameSink &sink) : sink_(sink) {}
    void configure(const media::AudioFormat &format) {
        drainConverter();
        inputFormat_ = std::make_unique<media::AudioFormat>(format);
        converter_ = std::make_unique<media::AudioConverter>(format);
    }
    void setSessionStartUs(int64_t time) {
        sessionStartUs_ = time;
        // The first packet and idle padding must share the same sample origin;
        // otherwise a delayed device start makes later silence timestamps go back.
        alignNextReal_ = time != AV_NOPTS_VALUE;
    }
    void setCaptureTimeUs(int64_t time) { captureUs_ = time; }
    bool packet(const AVFrame &frame, int64_t captureUs = AV_NOPTS_VALUE) {
        if (aborted_ || finished_) return false;
        if (!converter_) converter_ = std::make_unique<media::AudioConverter>(*inputFormat_);
        captureUs_ = captureUs == AV_NOPTS_VALUE ? AV_NOPTS_VALUE : captureUs - converter_->delayUs();
        const bool ok = deliverReal(converter_->convert(frame));
        converterTailUs_ = captureUs_;
        return ok;
    }
    int64_t samples() const { return samples_; }
    int64_t generatedSilenceSamples() const { return generatedSilenceSamples_; }
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
            const int count = frame->nb_samples;
            if (!deliver(std::move(frame))) return false;
            generatedSilenceSamples_ += count;
        }
        return !aborted_;
    }
    // Called only after SilencePolicy confirms idle. Emit real resampler tail
    // before silence; a fresh converter accepts the next packet after this flush.
    bool idleUntil(int64_t target) {
        if (target <= samples_ || aborted_ || finished_) return !aborted_;
        drainConverter();
        if (aborted_) return false;
        converter_.reset(); // Recreate lazily on the next real packet, not every idle poll.
        if (target <= samples_) return true;
        alignNextReal_ = true;
        captureUs_ = sessionTime(samples_);
        return silenceUntil(target);
    }
    // Terminal flush precedes any policy-approved tail padding and EOF. Return
    // generated silence only (exclude real samples released by the resampler).
    int64_t stop(bool abort, int64_t silenceTarget = 0, int64_t sessionStartUs = AV_NOPTS_VALUE) {
        if (abort) { aborted_ = true; return 0; }
        if (finished_ || aborted_) return 0;
        const auto before = generatedSilenceSamples_;
        drainConverter();
        if (!aborted_ && silenceTarget > samples_) {
            captureUs_ = sessionStartUs == AV_NOPTS_VALUE ? AV_NOPTS_VALUE :
                sessionStartUs + av_rescale_q(samples_, {1,kOutputSampleRate}, {1,1000000});
            silenceUntil(silenceTarget);
        }
        if (!aborted_ && !finished_) { finished_ = true; sink_.finish(); }
        return generatedSilenceSamples_ - before;
    }
private:
    int64_t sessionTime(int64_t sample) const {
        return sessionStartUs_ == AV_NOPTS_VALUE ? AV_NOPTS_VALUE : sessionStartUs_ +
            av_rescale_q(sample, {1,kOutputSampleRate}, {1,1000000});
    }
    bool deliverReal(media::FramePtr frame) {
        if (aborted_) return false;
        if (!frame) return true;
        if (alignNextReal_ && sessionStartUs_ != AV_NOPTS_VALUE && captureUs_ != AV_NOPTS_VALUE) {
            const int64_t first = av_rescale_q(captureUs_ - sessionStartUs_,
                                             {1,1000000}, {1,kOutputSampleRate});
            if (first > samples_) {
                // Packet timestamp confirms a startup gap or the remaining idle margin.
                captureUs_ = sessionTime(samples_);
                if (!silenceUntil(first)) return false;
            }
            const int skip = static_cast<int>(std::min<int64_t>(frame->nb_samples, samples_ - first));
            if (skip == frame->nb_samples) {
                // Entire late packet overlaps already delivered silence. Keep aligning
                // subsequent output (including converter tail) until it catches up.
                captureUs_ += av_rescale_q(skip, {1,kOutputSampleRate}, {1,1000000});
                return true;
            }
            if (skip > 0) {
                for (int ch = 0; ch < kOutputChannels; ++ch)
                    std::memmove(frame->extended_data[ch], frame->extended_data[ch] + skip*sizeof(float),
                                 (frame->nb_samples - skip)*sizeof(float));
                frame->nb_samples -= skip;
            }
            captureUs_ = sessionTime(samples_);
            alignNextReal_ = false;
        }
        return deliver(std::move(frame));
    }
    bool deliver(media::FramePtr frame) {
        if (aborted_) return false;
        if (!frame) return true;
        frame->pts = samples_;
        frame->time_base = {1, kOutputSampleRate};
        const int count = frame->nb_samples;
        frame->duration = count;
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
            if (!deliverReal(std::move(frame))) break;
        }
    }
    AudioFrameSink &sink_;
    std::unique_ptr<media::AudioConverter> converter_;
    std::unique_ptr<media::AudioFormat> inputFormat_;
    int64_t samples_ = 0;
    int64_t generatedSilenceSamples_ = 0;
    int64_t sessionStartUs_ = AV_NOPTS_VALUE;
    int64_t captureUs_ = AV_NOPTS_VALUE;
    int64_t converterTailUs_ = AV_NOPTS_VALUE;
    bool aborted_ = false, finished_ = false;
    bool alignNextReal_ = false;
};
}

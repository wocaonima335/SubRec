#include "audio_timeline_mixer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
}
namespace {
void check(int code) { if (code < 0) { char text[AV_ERROR_MAX_STRING_SIZE]; av_strerror(code, text, sizeof(text)); throw std::runtime_error(text); } }
}
AudioTimelineMixer::AudioTimelineMixer(int64_t startUs) : startUs_(startUs) {
    graph_ = avfilter_graph_alloc();
    if (!graph_) throw std::bad_alloc();
    try {
        for (int i = 0; i < 2; ++i)
            check(avfilter_graph_create_filter(&sources_[i], avfilter_get_by_name("abuffer"), i ? "mic" : "system",
                "time_base=1/48000:sample_rate=48000:sample_fmt=fltp:channel_layout=stereo", nullptr, graph_));
        AVFilterContext *mix = nullptr;
        check(avfilter_graph_create_filter(&mix, avfilter_get_by_name("amix"), "mix",
            "inputs=2:normalize=0:duration=longest:dropout_transition=0", nullptr, graph_));
        sink_ = avfilter_graph_alloc_filter(graph_, avfilter_get_by_name("abuffersink"), "out");
        if (!sink_) throw std::bad_alloc();
        const AVSampleFormat format = AV_SAMPLE_FMT_FLTP;
        check(av_opt_set_array(sink_, "sample_formats", AV_OPT_SEARCH_CHILDREN, 0, 1, AV_OPT_TYPE_SAMPLE_FMT, &format));
        check(avfilter_init_str(sink_, nullptr));
        check(avfilter_link(sources_[0], 0, mix, 0));
        check(avfilter_link(sources_[1], 0, mix, 1));
        check(avfilter_link(mix, 0, sink_, 0));
        check(avfilter_graph_config(graph_, nullptr));
    } catch (...) { avfilter_graph_free(&graph_); throw; }
}
AudioTimelineMixer::~AudioTimelineMixer() {
    for (auto &input : inputs_) { swr_free(&input.swr); av_channel_layout_uninit(&input.layout); }
    avfilter_graph_free(&graph_);
}
AudioTimelineMixer::Frame AudioTimelineMixer::allocate(int samples, int64_t pts) {
    Frame frame(av_frame_alloc());
    if (!frame) throw std::bad_alloc();
    frame->format = AV_SAMPLE_FMT_FLTP; frame->sample_rate = 48000;
    frame->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    frame->time_base = {1, 48000}; frame->pts = pts; frame->nb_samples = samples;
    check(av_frame_get_buffer(frame.get(), 0));
    return frame;
}
void AudioTimelineMixer::append(unsigned source, Frame frame) {
    if (!frame->nb_samples) return;
    end_ = std::max(end_, frame->pts + frame->nb_samples);
    inputs_[source].tailPts = frame->pts + frame->nb_samples;
    // A producer cannot consume unbounded memory if its clock jumps into the future.
    if (frame->pts > cursor_ + 48000 * 2) throw std::runtime_error("Audio capture clock is more than 2 s ahead");
    auto &frames = inputs_[source].frames;
    const auto at = std::upper_bound(frames.begin(), frames.end(), frame->pts,
        [](int64_t pts, const Frame &queued) { return pts < queued->pts; });
    frames.insert(at, std::move(frame));
}
void AudioTimelineMixer::push(unsigned source, const AVFrame &frame) {
    if (source > 1 || frame.pts == AV_NOPTS_VALUE || frame.time_base.num <= 0 || frame.time_base.den <= 0 ||
        frame.nb_samples <= 0 || frame.sample_rate <= 0 || !av_channel_layout_check(&frame.ch_layout))
        throw std::invalid_argument("Missing audio capture timestamp/format");
    auto &input = inputs_[source];
    const int64_t us = av_rescale_q(frame.pts, frame.time_base, {1, 1000000});
    bool changed = input.rate != frame.sample_rate || input.format != frame.format ||
                   av_channel_layout_compare(&input.layout, &frame.ch_layout);
    // Retain device PTS on every packet. Gaps insert silence, overlaps are trimmed
    // when rendering: hardware clock drift cannot accumulate into inter-source skew.
    if (changed || (input.nextUs != AV_NOPTS_VALUE && std::llabs(us - input.nextUs) > 2000)) {
        finish(source);
        swr_free(&input.swr); av_channel_layout_uninit(&input.layout);
        check(av_channel_layout_copy(&input.layout, &frame.ch_layout));
        input.rate = frame.sample_rate; input.format = frame.format;
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        check(swr_alloc_set_opts2(&input.swr, &stereo, AV_SAMPLE_FMT_FLTP, 48000,
            &input.layout, static_cast<AVSampleFormat>(frame.format), frame.sample_rate, 0, nullptr));
        check(swr_init(input.swr));
    }
    const int64_t delay = swr_get_delay(input.swr, 1000000);
    auto out = allocate(swr_get_out_samples(input.swr, frame.nb_samples),
                        av_rescale_q(us - startUs_ - delay, {1,1000000}, {1,48000}));
    int n = swr_convert(input.swr, out->extended_data, out->nb_samples,
                        const_cast<const uint8_t **>(frame.extended_data), frame.nb_samples);
    check(n); out->nb_samples = n;
    input.nextUs = us + av_rescale_q(frame.nb_samples, {1,frame.sample_rate}, {1,1000000});
    ++stats_.frames[source]; append(source, std::move(out));
}
void AudioTimelineMixer::finish(unsigned source) {
    auto &input = inputs_.at(source);
    if (!input.swr) return;
    for (;;) {
        auto frame = allocate(std::max(480, swr_get_out_samples(input.swr, 0)), input.tailPts);
        int n = swr_convert(input.swr, frame->extended_data, frame->nb_samples, nullptr, 0);
        check(n); if (!n) break;
        frame->nb_samples = n; append(source, std::move(frame));
    }
}
AudioTimelineMixer::Frame AudioTimelineMixer::render(int64_t target, double systemGain, double micGain) {
    if (target <= cursor_) return {};
    int count = static_cast<int>(std::min<int64_t>(480, target - cursor_));
    for (unsigned source = 0; source < 2; ++source) {
        auto frame = allocate(count, cursor_);
        av_samples_set_silence(frame->extended_data, 0, count, 2, AV_SAMPLE_FMT_FLTP);
        auto &frames = inputs_[source].frames;
        int filled = 0;
        while (!frames.empty()) {
            auto &in = frames.front();
            if (in->pts >= cursor_ + count) break;
            const int64_t begin = std::max(in->pts, cursor_ + filled);
            const int64_t end = std::min(in->pts + in->nb_samples, cursor_ + count);
            if (in->pts < cursor_ + filled) stats_.trimmed[source] += std::min<int64_t>(in->nb_samples, cursor_ + filled - in->pts);
            if (begin < end) {
                const double gain = std::max(0.0, source ? micGain : systemGain);
                for (int ch = 0; ch < 2; ++ch) {
                    auto *dst = reinterpret_cast<float *>(frame->extended_data[ch]);
                    auto *src = reinterpret_cast<const float *>(in->extended_data[ch]);
                    for (int64_t t = begin; t < end; ++t) dst[t-cursor_] = src[t-in->pts] * gain;
                }
                stats_.silence[source] += begin - (cursor_ + filled);
                filled = end - cursor_;
            }
            if (in->pts + in->nb_samples <= cursor_ + count) frames.pop_front();
            else {
                // Consume only the prefix covered by this render window. Keeping
                // an offset prevents previously rendered samples counting as overlap.
                const int consumed = static_cast<int>(end - in->pts);
                if (consumed > 0) {
                    for (int ch = 0; ch < 2; ++ch)
                        std::memmove(in->extended_data[ch], in->extended_data[ch] + consumed * sizeof(float),
                                     (in->nb_samples - consumed) * sizeof(float));
                    in->pts += consumed; in->nb_samples -= consumed;
                }
                break;
            }
        }
        stats_.silence[source] += count - filled;
        check(av_buffersrc_add_frame_flags(sources_[source], frame.get(), 0));
    }
    Frame out(av_frame_alloc()); if (!out) throw std::bad_alloc();
    check(av_buffersink_get_frame(sink_, out.get()));
    // Stereo-linked sample-peak limiter: immediate attack, 50 ms exponential
    // release, -0.45 dBFS ceiling. Attenuates both channels; never hard-clips peaks.
    auto *left = reinterpret_cast<float *>(out->extended_data[0]);
    auto *right = reinterpret_cast<float *>(out->extended_data[1]);
    const double release = std::exp(-1.0 / (0.050 * 48000));
    for (int i = 0; i < out->nb_samples; ++i) {
        if (!std::isfinite(left[i])) left[i] = 0;
        if (!std::isfinite(right[i])) right[i] = 0;
        const double peak = std::max(std::abs(left[i]), std::abs(right[i]));
        const double needed = peak > 0.95 ? 0.95 / peak : 1;
        limiterGain_ = std::min(needed, 1 - (1 - limiterGain_) * release);
        if (limiterGain_ < 0.999999) ++stats_.limited;
        left[i] *= limiterGain_; right[i] *= limiterGain_;
    }
    out->time_base = {1,48000}; cursor_ += count;
    return out;
}

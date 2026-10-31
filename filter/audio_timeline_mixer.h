#pragma once
#include <array>
#include <deque>
#include <cstdint>
#include <memory>
extern "C" {
#include <libavfilter/avfilter.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
}

class AudioMixDump;

// Single worker owns this graph. Input PTS/time_base describes capture time on the
// av_gettime_relative clock, NOT arrival at this queue. Output PTS starts at zero.
class AudioTimelineMixer {
public:
    // Sample-peak ceiling (-3 dBFS) for native AAC at audio_encoding::kAacBitRate
    // (128*1024 bps). Codec reconstruction peaks are checked by audio_mix_aac_tests.
    static constexpr double limiterCeiling = 0.7079;
    struct FrameDelete { void operator()(AVFrame *f) const { av_frame_free(&f); } };
    using Frame = std::unique_ptr<AVFrame, FrameDelete>;
    static constexpr int64_t resyncThresholdUs = 10000;
    struct Stats {
        uint64_t frames[2]{}, silence[2]{}, trimmed[2]{}, resyncs[2]{};
        // Stereo sample frames at the limiter input (after source gains/amix).
        // Keep limited for existing callers, but count only frames where either
        // channel exceeds the ceiling, never gain attenuation during release.
        // Denominator includes all rendered frames, including silence/paused output.
        uint64_t limited = 0, limiterSamples = 0;
        double maxGainReductionDb = 0;
        double overCeilingRatio() const {
            return limiterSamples ? double(limited) / limiterSamples : 0;
        }
        uint64_t futurePacketsDropped[2]{};
        double compensationPpm[2]{}, phaseErrorUs[2]{};
    };
    explicit AudioTimelineMixer(int64_t startUs);
    ~AudioTimelineMixer();
    void push(unsigned source, const AVFrame &frame);
    void finish(unsigned source);
    // Target samples from session start. Caller waits 200 ms for late packets.
    // retainedOutput=false for paused/discarded frames: omit them from the debug
    // dump so dump sample N matches the recorded mix sample N after pause removal.
    Frame render(int64_t target, double systemGain = 1, double micGain = 1, bool retainedOutput = true);
    int64_t position() const { return cursor_; }
    int64_t end() const { return end_; }
    size_t pendingFrames(unsigned source) const { return inputs_.at(source).frames.size(); }
    const Stats &stats() const { return stats_; }
private:
    struct Input {
        SwrContext *swr = nullptr;
        AVChannelLayout layout{};
        int rate = 0, format = -1;
        // Fractional microseconds avoid accumulating per-packet rounding error.
        long double nextUs = AV_NOPTS_VALUE;
        int64_t tailPts = 0;
        double errorIntegralPpm = 0, observationUs = 0, weightedError = 0;
        int compensationDelta = 0;
        bool finished = false;
        std::deque<Frame> frames;
    };
    Frame allocate(int samples, int64_t pts);
    void append(unsigned source, Frame frame);
    void compensate(unsigned source, double errorUs, double durationUs);
    std::array<Input, 2> inputs_;
    AVFilterGraph *graph_ = nullptr;
    AVFilterContext *sources_[2]{}, *sink_ = nullptr;
    int64_t startUs_, cursor_ = 0, end_ = 0;
    const double releaseCoeff_;
    double limiterGain_ = 1;
    Stats stats_;
    std::unique_ptr<AudioMixDump> dump_;
};

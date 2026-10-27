// FFAFilter audio-graph regression test with synthetic frames (no devices, no recording).
// Covers: abuffersink "sample_formats" restriction (output must be FLTP even for S16 input),
// the dual graph abuffer -> volume -> amix(pad 0 / pad 1) -> abuffersink, runtime volume
// commands, and ownership/error paths of processDualtAudioFrames().
#include "filter/ffafilter.h"
#include "filter/audio_timeline_mixer.h"
#include "queue/ffaframequeue.h"
#include "thread/ffafilterthread.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

#define CHECK(test) do { if (!(test)) throw std::runtime_error(std::string(#test) + " (line " + std::to_string(__LINE__) + ")"); } while (false)

namespace {

constexpr int kFrameSamples = 1024;
constexpr float kSystemLevel = 0.4f;
constexpr float kMicrophoneLevel = 0.2f;
constexpr float kTolerance = 1e-4f;

// Constant ("DC") stereo FLTP frame, so the mixed value can be predicted exactly.
AVFrame *makeFltpFrame(float level, int64_t pts)
{
    AVFrame *frame = av_frame_alloc();
    CHECK(frame);
    frame->format = AV_SAMPLE_FMT_FLTP;
    frame->sample_rate = AUDIO_SAMPLE_RATE;
    frame->nb_samples = kFrameSamples;
    frame->pts = pts;
    av_channel_layout_default(&frame->ch_layout, 2);
    CHECK(av_frame_get_buffer(frame, 0) == 0);
    for (int channel = 0; channel < 2; ++channel) {
        auto *samples = reinterpret_cast<float *>(frame->extended_data[channel]);
        for (int i = 0; i < kFrameSamples; ++i) {
            samples[i] = level;
        }
    }
    return frame;
}

AVFrame *makeS16Frame(int16_t level, int64_t pts)
{
    AVFrame *frame = av_frame_alloc();
    CHECK(frame);
    frame->format = AV_SAMPLE_FMT_S16;
    frame->sample_rate = AUDIO_SAMPLE_RATE;
    frame->nb_samples = kFrameSamples;
    frame->pts = pts;
    av_channel_layout_default(&frame->ch_layout, 2);
    CHECK(av_frame_get_buffer(frame, 0) == 0);
    auto *samples = reinterpret_cast<int16_t *>(frame->data[0]);
    for (int i = 0; i < kFrameSamples * 2; ++i) {
        samples[i] = level;
    }
    return frame;
}

// Stand-ins for what FFADecoder would provide: only ch_layout and the stream time_base are read.
struct FakeSource
{
    AVFormatContext *format = nullptr;
    AVStream *stream = nullptr;
    AVCodecContext *codec = nullptr;

    FakeSource()
    {
        format = avformat_alloc_context();
        CHECK(format);
        stream = avformat_new_stream(format, nullptr);
        CHECK(stream);
        stream->time_base = AVRational{1, AUDIO_SAMPLE_RATE};
        codec = avcodec_alloc_context3(nullptr);
        CHECK(codec);
        codec->sample_rate = AUDIO_SAMPLE_RATE;
        codec->sample_fmt = AUDIO_SAMPLE_FORMAT;
        av_channel_layout_default(&codec->ch_layout, 2);
    }
    ~FakeSource()
    {
        avcodec_free_context(&codec);
        avformat_free_context(format);
    }
    FakeSource(const FakeSource &) = delete;
    FakeSource &operator=(const FakeSource &) = delete;
};

struct DrainResult
{
    int frames = 0;
    int samples = 0;
    float lastLeft = NAN;
    float lastRight = NAN;
};

// Takes every frame the filter has queued so far; each must be 48 kHz stereo FLTP and flat.
DrainResult drain(FFAFrameQueue &queue)
{
    DrainResult result;
    while (!queue.peekEmpty()) {
        auto received = queue.receive();
        CHECK(received.ok());
        AVFrame *frame = received.item;
        CHECK(frame->format == AV_SAMPLE_FMT_FLTP);
        CHECK(frame->sample_rate == AUDIO_SAMPLE_RATE);
        CHECK(frame->ch_layout.nb_channels == 2);
        CHECK(frame->nb_samples > 0);
        const auto *left = reinterpret_cast<const float *>(frame->extended_data[0]);
        const auto *right = reinterpret_cast<const float *>(frame->extended_data[1]);
        for (int i = 1; i < frame->nb_samples; ++i) {
            CHECK(std::fabs(left[i] - left[0]) < kTolerance);
            CHECK(std::fabs(right[i] - right[0]) < kTolerance);
        }
        result.frames += 1;
        result.samples += frame->nb_samples;
        result.lastLeft = left[frame->nb_samples - 1];
        result.lastRight = right[frame->nb_samples - 1];
        AVFrameTraits::release(frame);
    }
    return result;
}

// Feeds `pairs` frame pairs and returns everything mixed meanwhile.
DrainResult feed(FFAFilter &filter, FFAFrameQueue &queue, int pairs, int64_t &pts)
{
    DrainResult total;
    for (int i = 0; i < pairs; ++i) {
        CHECK(filter.processDualtAudioFrames(makeFltpFrame(kSystemLevel, pts),
                                             makeFltpFrame(kMicrophoneLevel, pts),
                                             0,
                                             0)
              == 0);
        pts += kFrameSamples;
        // Drain after every pair: the queue holds 30 frames and enqueue() blocks when full.
        DrainResult part = drain(queue);
        total.frames += part.frames;
        total.samples += part.samples;
        if (part.frames > 0) {
            total.lastLeft = part.lastLeft;
            total.lastRight = part.lastRight;
        }
    }
    return total;
}

bool near(float value, float expected)
{
    return std::fabs(value - expected) < kTolerance;
}

// abuffer(S16) -> FFAFilter::createBufferSinkFilter(): with the sample_formats restriction
// applied the graph must insert a converter and deliver FLTP. FFmpeg 8's sample_formats
// is a private array option: looking it up on AVFilterContext without SEARCH_CHILDREN
// fails with OPTION_NOT_FOUND. Omitting the restriction would return S16 unchanged.
void sinkRestrictsToFltp()
{
    FFAFilter filter;
    AVFilterGraph *graph = avfilter_graph_alloc();
    CHECK(graph);
    AVFilterContext *source = nullptr;
    AVFilterContext *sink = nullptr;
    AVFrame *output = nullptr;
    try {
        const std::string args = "time_base=1/48000:sample_rate=48000:sample_fmt=s16:"
                                 "channel_layout=stereo";
        CHECK(avfilter_graph_create_filter(&source,
                                           avfilter_get_by_name("abuffer"),
                                           "in",
                                           args.c_str(),
                                           nullptr,
                                           graph)
              >= 0);
        filter.createBufferSinkFilter(graph, &sink, "out");
        CHECK(sink);
        CHECK(avfilter_link(source, 0, sink, 0) == 0);
        avfilter_graph_set_auto_convert(graph, AVFILTER_AUTO_CONVERT_ALL);
        CHECK(avfilter_graph_config(graph, nullptr) >= 0);
        CHECK(av_buffersink_get_format(sink) == AV_SAMPLE_FMT_FLTP);
        CHECK(av_buffersink_get_sample_rate(sink) == AUDIO_SAMPLE_RATE);

        AVFrame *input = makeS16Frame(16384, 0);
        CHECK(av_buffersrc_add_frame_flags(source, input, 0) >= 0);
        av_frame_free(&input);
        output = av_frame_alloc();
        CHECK(output);
        CHECK(av_buffersink_get_frame(sink, output) >= 0);
        CHECK(output->format == AV_SAMPLE_FMT_FLTP);
        CHECK(output->sample_rate == AUDIO_SAMPLE_RATE);
        CHECK(output->ch_layout.nb_channels == 2);
        CHECK(output->pts == 0);
        CHECK(output->nb_samples == kFrameSamples);
        for (int channel = 0; channel < 2; ++channel) {
            const auto *samples = reinterpret_cast<const float *>(output->extended_data[channel]);
            for (int i = 0; i < output->nb_samples; ++i) {
                CHECK(near(samples[i], 0.5f));
            }
        }
    } catch (...) {
        av_frame_free(&output);
        avfilter_graph_free(&graph);
        throw;
    }
    av_frame_free(&output);
    avfilter_graph_free(&graph);
    std::cout << "PASS: abuffersink sample_formats -> fltp\n";
}

// The dual graph must configure (it could not with both volumes on amix pad 0) and mix
// both inputs: amix normalizes two inputs by 1/2 each, so 0.4 and 0.2 give 0.3.
void dualGraphMixesBothInputs()
{
    FFAFrameQueue queue;
    FFAFilter filter;
    filter.initalize(&queue, nullptr, nullptr);
    FakeSource system;
    FakeSource microphone;
    filter.initializeDualAudioFilter(system.codec, microphone.codec, system.stream,
                                     microphone.stream);

    int64_t pts = 0;
    DrainResult mixed = feed(filter, queue, 16, pts);
    CHECK(mixed.frames > 0);
    CHECK(mixed.samples > kFrameSamples * 8); // amix may hold back at most a frame or two
    CHECK(mixed.samples <= kFrameSamples * 16);
    CHECK(near(mixed.lastLeft, 0.5f * kSystemLevel + 0.5f * kMicrophoneLevel));
    CHECK(near(mixed.lastRight, 0.5f * kSystemLevel + 0.5f * kMicrophoneLevel));
    std::cout << "PASS: dual graph mixes both inputs (" << mixed.samples << " samples, value "
              << mixed.lastLeft << ")\n";

    // Volume changes go through the volume filter's runtime command (eval=frame). Frames
    // already inside amix keep the old gain, so only the last output frame is checked.
    filter.setSystemAudioVolume(0.0);
    DrainResult systemMuted = feed(filter, queue, 8, pts);
    CHECK(systemMuted.frames > 0);
    CHECK(near(systemMuted.lastLeft, 0.5f * kMicrophoneLevel));

    filter.setSystemAudioVolume(1.0);
    filter.setMicrophoneAudioVolume(0.0);
    DrainResult microphoneMuted = feed(filter, queue, 8, pts);
    CHECK(microphoneMuted.frames > 0);
    CHECK(near(microphoneMuted.lastLeft, 0.5f * kSystemLevel));
    std::cout << "PASS: runtime volume per source (system muted " << systemMuted.lastLeft
              << ", microphone muted " << microphoneMuted.lastLeft << ")\n";
}

// Production worker consumes both queues and sends a single EOF after both drain.
void mixedWorkerDrainsBothSources(bool paused = false)
{
    FFAFrameQueue system, microphone, output;
    FFAFilter filter;
    filter.initalize(&output, nullptr, nullptr);
    const int64_t epoch = av_gettime_relative();
    auto *sys = makeFltpFrame(kSystemLevel, epoch);
    auto *mic = makeFltpFrame(kMicrophoneLevel, epoch);
    sys->time_base = mic->time_base = AVRational{1,1000000};
    system.enqueue(sys); microphone.enqueue(mic);
    av_frame_free(&sys); av_frame_free(&mic);
    system.sendFinish(); microphone.sendFinish();
    FFAFilterThread worker;
    worker.init(&microphone, &system, &filter);
    worker.configureMix(true, epoch);
    worker.startEncoder();
    if (paused) {
        worker.onPauseChanged(true, epoch + 5000);
        worker.onPauseChanged(false, epoch + 15000);
    }
    worker.start(); worker.wait();
    auto mixed = drain(output);
    CHECK(mixed.samples == kFrameSamples - (paused ? 480 : 0));
    CHECK(near(mixed.lastLeft, (kSystemLevel + kMicrophoneLevel) * AudioTimelineMixer::aacHeadroomGain));
    CHECK(near(mixed.lastRight, (kSystemLevel + kMicrophoneLevel) * AudioTimelineMixer::aacHeadroomGain));
    CHECK(output.receive().result == MediaQueueResult::Eof);
    std::cout << "PASS: mixed worker consumes both sources and drains one output\n";
}

// Error/ownership paths must neither crash nor leak a half-built graph.
void dualErrorPaths()
{
    FFAFrameQueue queue;
    FFAFilter filter;
    filter.initalize(&queue, nullptr, nullptr);

    // A missing input frame is a no-op that still releases the other one.
    CHECK(filter.processDualtAudioFrames(makeFltpFrame(kSystemLevel, 0), nullptr, 0, 0) == 0);
    // No graph and no decoders to build one: refused, inputs released.
    CHECK(filter.processDualtAudioFrames(makeFltpFrame(kSystemLevel, 0),
                                         makeFltpFrame(kMicrophoneLevel, 0),
                                         0,
                                         0)
          == -1);

    // Invalid arguments throw and leave no graph behind (still refused afterwards).
    FakeSource source;
    bool threw = false;
    try {
        filter.initializeDualAudioFilter(source.codec, nullptr, source.stream, nullptr);
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    CHECK(threw);
    CHECK(filter.processDualtAudioFrames(makeFltpFrame(kSystemLevel, 0),
                                         makeFltpFrame(kMicrophoneLevel, 0),
                                         0,
                                         0)
          == -1);
    CHECK(queue.peekEmpty());

    // Re-initializing replaces the graph instead of leaking or double-linking it.
    FakeSource system;
    FakeSource microphone;
    filter.initializeDualAudioFilter(system.codec, microphone.codec, system.stream,
                                     microphone.stream);
    filter.initializeDualAudioFilter(system.codec, microphone.codec, system.stream,
                                     microphone.stream);
    int64_t pts = 0;
    CHECK(feed(filter, queue, 4, pts).frames > 0);
    std::cout << "PASS: dual error and ownership paths\n";
}

} // namespace

int main()
{
    try {
        sinkRestrictsToFltp();
        dualGraphMixesBothInputs();
        dualErrorPaths();
        mixedWorkerDrainsBothSources();
        mixedWorkerDrainsBothSources(true);
        std::cout << "PASS: audio filter dual\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

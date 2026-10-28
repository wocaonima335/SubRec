#pragma once
#include <cstdint>
#include <chrono>
struct AVFrame;
namespace capture::wasapi {
// Fixed output of the source: what FFADecoder delivers today and what FFAFilter /
// FFAEncoder::initAudio expect (filter/ffafilter.h AUDIO_SAMPLE_RATE / AUDIO_SAMPLE_FORMAT).
constexpr int kOutputSampleRate = 48000;
constexpr int kOutputChannels = 2;          // AV_CHANNEL_LAYOUT_STEREO
// Output is AV_SAMPLE_FMT_FLTP. AudioPipeline::deliver (wasapi_audio_pipeline.h)
// sets pts to the delivered sample count and time_base to 1/kOutputSampleRate.
// When capture time is known, it attaches AVDictionary metadata capture_time_us
// in microseconds on the av_gettime_relative clock.
constexpr int kOutputFrameSamples = kOutputSampleRate / 100;    // 10 ms per AVFrame (any size is accepted downstream)

// QueueAudioSink retains the shared state of FFAFrameQueue (aFrmQueue[A_AUDIO]):
// LoopbackCapture wraps this delivery-side sink in BufferedAudioSink; the capture
// thread never calls QueueAudioSink directly.
//   push   -> enqueueFromSrc (moves the reference; waits while full unless closed,
//             finished or cancelled; returns true only when the frame was queued)
//   finish -> sendFinish (EOF for FFAFilterThread, suppressed after cancellation)
// Single-source mode preserves the frame's pts/time_base. In mix mode
// (microphoneMixInput set), QueueAudioSink replaces pts with capture_time_us and
// time_base with 1/1000000. Missing metadata returns false before enqueueing.
// A false push return means abort: AudioPipeline stops delivering and omits EOF.
struct SinkDiagnostics {
    uint64_t blockedNs = 0, maxBlockedNs = 0, rejectedSamples = 0;
    uint64_t downstreamDroppedSamples = 0, downstreamDroppedFrames = 0;
    uint64_t downstreamSilenceSamples = 0;
    uint64_t deliveryBlockedNs = 0, deliveryMaxBlockedNs = 0;
};
class AudioFrameSink
{
public:
    virtual ~AudioFrameSink() = default;
    // Successful enqueue takes the frame's reference (av_frame_move_ref); it is
    // blank afterwards. Once passed to the queue, a rejected frame is also consumed
    // (av_frame_unref), including when the queue is closed. An already-cancelled
    // QueueAudioSink likewise unrefs the frame and returns false immediately.
    // Otherwise, in mix mode missing capture_time_us returns false WITHOUT
    // consuming the frame: the caller still owns it (AudioPipeline releases it).
    virtual bool push(AVFrame *frame) = 0;
    // BufferedAudioSink leaves an intentionally dropped frame owned by the caller
    // and returns true: the source timeline must advance across the dropped region.
    virtual void finish() = 0;
    // Control thread: ordered EOF/drain completion for an asynchronous sink.
    virtual bool joinFor(std::chrono::milliseconds) { return true; }
    // Nonblocking and idempotent. Wake blocked pushes and permanently reject late
    // push/finish calls, including after the recorder has restarted its queues.
    // A sink and everything it accesses must survive until the capture task exits.
    virtual void cancel() noexcept {}
    virtual SinkDiagnostics diagnostics() const { return {}; }
};

}

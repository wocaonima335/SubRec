#pragma once
struct AVFrame;
namespace capture::wasapi {
// Fixed output of the source: what FFADecoder delivers today and what FFAFilter /
// FFAEncoder::initAudio expect (filter/ffafilter.h AUDIO_SAMPLE_RATE / AUDIO_SAMPLE_FORMAT).
constexpr int kOutputSampleRate = 48000;
constexpr int kOutputChannels = 2;          // AV_CHANNEL_LAYOUT_STEREO
// Output sample format is AV_SAMPLE_FMT_FLTP; pts in 1/48000 (see LoopbackCapture::start()).
constexpr int kOutputFrameSamples = kOutputSampleRate / 100;    // 10 ms per AVFrame (any size is accepted downstream)

// Where frames go. The production adapter wraps FFAFrameQueue (aFrmQueue[A_AUDIO]):
//   push   -> FFAFrameQueue::enqueue (moves the reference; blocks while full; returns false
//             when the queue was closed by an abort)
//   finish -> FFAFrameQueue::sendFinish (EOF for FFAFilterThread)
class AudioFrameSink
{
public:
    virtual ~AudioFrameSink() = default;
    // Takes the frame's reference (av_frame_move_ref semantics); frame is blank afterwards.
    virtual bool push(AVFrame *frame) = 0;
    virtual void finish() = 0;
    // Nonblocking and idempotent. Wake blocked pushes and permanently reject late
    // push/finish calls, including after the recorder has restarted its queues.
    // A sink and everything it accesses must survive until the capture task exits.
    virtual void cancel() noexcept {}
};

}

#ifndef WASAPI_LOOPBACK_CAPTURE_H
#define WASAPI_LOOPBACK_CAPTURE_H

// DESIGN ONLY - not in any CMakeLists.txt and not included anywhere yet.
// Native system-audio source (WASAPI shared-mode loopback) that will replace the third-party
// DirectShow "virtual-audio-capturer". Full contract: docs/WASAPI_LOOPBACK_INTERFACE.md.
// Reference implementation of the raw API calls: tests/native_capture_probe/main.cpp (wasapi).
//
// Placement in the existing audio chain (same idea as FFScreenGrabThread for ddagrab):
//   today:  dshow demuxer -> aPktQueue[A_AUDIO] -> FFADecoder(+FFAResampler) -> aFrmQueue[A_AUDIO]
//   later:  wasapi::LoopbackCapture ------------------------------------------> aFrmQueue[A_AUDIO]
// FFAFilterThread / FFAFilter / FFAEncoderThread / RecorderSyncQueue stay unchanged.

#include "capture_types.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct AVFrame;

namespace capture::wasapi {

// Fixed output of the source: what FFADecoder delivers today and what FFAFilter /
// FFAEncoder::initAudio expect (filter/ffafilter.h AUDIO_SAMPLE_RATE / AUDIO_SAMPLE_FORMAT).
constexpr int kOutputSampleRate = 48000;
constexpr int kOutputChannels = 2;          // AV_CHANNEL_LAYOUT_STEREO
// Output sample format is AV_SAMPLE_FMT_FLTP; pts in 1/48000 (see LoopbackCapture::start()).
constexpr int kOutputFrameSamples = 480;    // 10 ms per AVFrame (any size is accepted downstream)

enum class EndpointRole
{
    Console,        // eConsole: the default "Speakers" (what virtual-audio-capturer follows)
    Multimedia,     // eMultimedia
    Communications  // eCommunications
};

struct LoopbackConfig
{
    // Empty: default render endpoint for `role`. Otherwise an IMMDevice id
    // ("{0.0.0.00000000}.{guid}") from LoopbackCapture::enumerateEndpoints().
    std::wstring endpointId;
    EndpointRole role = EndpointRole::Console;
    // IAudioClient::Initialize hnsBufferDuration. 100 ms like the probe; the queue between
    // source and FFAFilterThread holds 30 frames (300 ms at kOutputFrameSamples).
    std::chrono::milliseconds bufferDuration{100};
    // Event-driven (AUDCLNT_STREAMFLAGS_EVENTCALLBACK) when the OS supports it for loopback,
    // otherwise 5 ms polling as in the probe. The implementation decides and reports it.
    bool preferEventCallback = true;
    // Loopback delivers NO packets while nothing plays. RecorderSyncQueue uses audio as the
    // master clock (pts = sample count), so the source must synthesize silence for those gaps,
    // paced by QPC. Only tests may turn this off.
    bool fillIdleWithSilence = true;
    // Follow a change of the default device (IMMNotificationClient::OnDefaultDeviceChanged)
    // by reopening on the new endpoint, keeping the output timeline continuous.
    bool followDefaultDevice = true;
};

struct EndpointInfo
{
    std::wstring id;
    QString friendlyName; // PKEY_Device_FriendlyName
    bool isDefault = false;
};

// Device mix format (IAudioClient::GetMixFormat), for logs and diagnostics only; the output
// is always 48 kHz stereo FLTP.
struct MixFormat
{
    int sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 0;
    int validBitsPerSample = 0;
    std::uint32_t channelMask = 0;
    bool isFloat = false; // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT (the usual shared-mode mix format)
};

struct LoopbackStats
{
    std::uint64_t packets = 0;
    std::uint64_t deviceFrames = 0;       // frames read from IAudioCaptureClient
    std::uint64_t silentPacketFrames = 0; // AUDCLNT_BUFFERFLAGS_SILENT
    std::uint64_t generatedSilenceFrames = 0; // idle gaps filled (output rate)
    std::uint64_t discontinuities = 0;    // AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY
    std::uint64_t timestampErrors = 0;    // AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR
    std::uint64_t outputFrames = 0;       // samples per channel delivered at 48 kHz
    std::uint64_t sinkBlockedMs = 0;      // time spent waiting for the frame queue
    std::uint32_t reopenCount = 0;        // device invalidated / default device changed
};

enum class LoopbackState
{
    Idle,      // constructed or after join()
    Prepared,  // open() succeeded, stream initialized but not started
    Running,   // IAudioClient::Start called, delivering frames
    Stopping,  // requestDrainStop()/requestAbort() seen
    Finished,  // sink got finish() (drain) or was abandoned (abort)
    Failed     // open failed, or a runtime error that could not be recovered
};

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
};

struct LoopbackRuntimeError
{
    long hresult = 0;          // e.g. AUDCLNT_E_DEVICE_INVALIDATED, AUDCLNT_E_SERVICE_NOT_RUNNING
    QString operation;         // "GetBuffer", "Initialize", ...
    QString message;
    bool recovered = false;    // true: reopened and continued (timeline kept, gap = silence)
};

// One instance per recording. All COM objects live on the internal capture thread (MTA),
// created in open() and released before join() returns - same rule as the probe.
class LoopbackCapture
{
public:
    explicit LoopbackCapture(LoopbackConfig config);
    ~LoopbackCapture(); // requestAbort() + join()
    LoopbackCapture(const LoopbackCapture &) = delete;
    LoopbackCapture &operator=(const LoopbackCapture &) = delete;

    // Active render endpoints (for a future device list; ids for LoopbackConfig::endpointId).
    static std::vector<EndpointInfo> enumerateEndpoints();

    // Starts the capture thread, which does CoInitializeEx(MTA), endpoint lookup, Activate,
    // GetMixFormat, Initialize(LOOPBACK[, EVENTCALLBACK]), GetService, creates the PCM ->
    // 48 kHz stereo FLTP converter (capture::media::AudioConverter), then waits for start().
    // Blocks up to timeout for that result. Errors use CaptureErrorStage InputOpen /
    // InputFormat with backend = CaptureBackend::Wasapi (to be added) and hresult in
    // CaptureError::ffmpegError (negative HRESULT value) and a readable message.
    CaptureStartResult open(AudioFrameSink *sink,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    // IAudioClient::Start; the first delivered sample gets pts 0 and its QPC time becomes
    // startQpcUs(). Called from FFOpenSourceEvent::startAudio() where the dshow audio
    // demuxer/decoder threads are started today.
    void start();

    // Stop reading, flush the converter (AudioConverter::drain until nullptr), push the tail,
    // then sink->finish(). Used by FFCloseSourceEvent (normal stop).
    void requestDrainStop();
    // Stop reading and return without finish() (the caller closes the queue). Used by
    // CaptureSession::abortPipeline and failed starts.
    void requestAbort();
    // Wait for the capture thread; afterwards all COM objects are released.
    void join();

    LoopbackState state() const;
    MixFormat mixFormat() const;      // valid after a successful open()
    LoopbackStats stats() const;      // thread-safe snapshot
    // QPC time (microseconds, same clock as av_gettime_relative() on Windows only if verified;
    // the implementation stores its own QPC->av_gettime_relative offset) of output pts 0.
    std::int64_t startQpcUs() const;

    // Called on the capture thread for every runtime error (recovered or not); must not
    // block. Production: QMetaObject::invokeMethod(recorder, "setCaptureError", Queued) when
    // !recovered.
    std::function<void(const LoopbackRuntimeError &)> onRuntimeError;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace capture::wasapi

#endif // WASAPI_LOOPBACK_CAPTURE_H

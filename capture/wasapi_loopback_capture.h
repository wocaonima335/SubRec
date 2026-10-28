#ifndef WASAPI_LOOPBACK_CAPTURE_H
#define WASAPI_LOOPBACK_CAPTURE_H

// Implemented for WIN32; see the interface document for lifecycle and timing contracts.
// Native system-audio source (WASAPI shared-mode loopback) that will replace the third-party
// DirectShow "virtual-audio-capturer". Full contract: docs/WASAPI_LOOPBACK_INTERFACE.md.
// Reference implementation of the raw API calls: tests/native_capture_probe/main.cpp (wasapi).
//
// Placement in the existing audio chain (same idea as FFScreenGrabThread for ddagrab):
//   today:  dshow demuxer -> aPktQueue[A_AUDIO] -> FFADecoder(+FFAResampler) -> aFrmQueue[A_AUDIO]
//   later:  wasapi::LoopbackCapture ------------------------------------------> aFrmQueue[A_AUDIO]
// FFAFilterThread / FFAFilter / FFAEncoderThread / RecorderSyncQueue stay unchanged.

#include "capture_types.h"
#include "wasapi_audio_sink.h"
#include "wasapi_worker_lifetime.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct AVFrame;

namespace capture::wasapi {

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
    // After >=100 ms and >=3 consecutive empty polls, advance the session timeline
    // with silence, reserving 100 ms for late data plus the converter's pending tail.
    // This also advances the audio master before the first packet and after sound
    // stops. SILENT packets are delivered normally and reset idle qualification.
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
    std::uint64_t driftWarnings = 0;
    std::uint64_t timestampErrors = 0;    // AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR
    std::uint64_t outputFrames = 0;       // samples per channel delivered at 48 kHz
    double sinkBlockedMs = 0, sinkBlockedMaxMs = 0; // capture handoff (zero with BufferedAudioSink)
    std::uint64_t estimatedLostSamples = 0; // flagged device-position gaps, converted to 48 kHz
    std::uint64_t unquantifiedDiscontinuities = 0; // no reliable device-position baseline
    std::uint64_t trimmedSamples = 0;     // actual overlap removed at 48 kHz
    std::uint64_t rejectedSamples = 0;    // frames rejected by the sink, at 48 kHz
    std::uint64_t downstreamDroppedSamples = 0, downstreamDroppedFrames = 0;
    std::uint64_t downstreamSilenceSamples = 0;
    double deliveryBlockedMs = 0, deliveryBlockedMaxMs = 0; // delivery worker only
    std::uint32_t reopenCount = 0;        // device invalidated / default device changed
};

enum class LoopbackState
{
    Idle,      // constructed, not opened yet (instances are single-use)
    Prepared,  // open() succeeded, stream initialized but not started
    Running,   // IAudioClient::Start called, delivering frames
    Stopping,  // requestDrainStop()/requestAbort() seen
    Finished,  // sink got finish() (drain) or was abandoned (abort)
    Failed     // open failed, or a runtime error that could not be recovered
};

struct LoopbackRuntimeError
{
    long hresult = 0;          // e.g. AUDCLNT_E_DEVICE_INVALIDATED, AUDCLNT_E_SERVICE_NOT_RUNNING
    QString operation;         // "GetBuffer", "Initialize", ...
    QString message;
    bool recovered = false;    // true: reopened and continued (timeline kept, gap = silence)
};

// One instance per recording. All COM objects live on the internal capture thread (MTA),
// created in open() and released there. A timed-out worker retains its own state
// and sink until it exits; the recorder is never kept waiting indefinitely.
class LoopbackCapture
{
public:
    explicit LoopbackCapture(LoopbackConfig config);
    ~LoopbackCapture(); // abort + bounded join, then detach if the driver is stuck
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
    CaptureStartResult open(std::shared_ptr<AudioFrameSink> sink,
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
    // Wait at most 500 ms, then detach on timeout. Never resets this instance to Idle.
    void join();
    bool joinFor(std::chrono::milliseconds timeout);
    void abandon(); // abort + detach; marks the instance Failed permanently

    LoopbackState state() const;
    MixFormat mixFormat() const;      // valid after a successful open()
    LoopbackStats stats() const;      // thread-safe snapshot
    // QPC time (microseconds, same clock as av_gettime_relative() on Windows only if verified;
    // the implementation stores its own QPC->av_gettime_relative offset) of output pts 0.
    std::int64_t startQpcUs() const;

    // Called on the capture thread for every runtime error (recovered or not); must not
    // block. Production: QMetaObject::invokeMethod(recorder, "setCaptureError", Queued) when
    // !recovered. Set before open(); copied into the worker. Captured targets must
    // have independent lifetime protection (the worker can outlive this object).
    std::function<void(const LoopbackRuntimeError &)> onRuntimeError;

private:
    struct Impl;
    std::shared_ptr<Impl> d;
    WorkerLifetime worker_;
};

} // namespace capture::wasapi

#endif // WASAPI_LOOPBACK_CAPTURE_H

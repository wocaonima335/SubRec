// Instantiate SDK property keys (notably PKEY_Device_FriendlyName) here:
// MinGW's uuid/propsys libraries do not supply them. KS format GUIDs use
// DEFINE_GUIDEX instead and are supplied by capture_module's ksuser dependency.
// Keep this before any header that could transitively include the Windows SDK;
// do not put INITGUID in the shared wasapi_native.h header.
#include <initguid.h>

#include "wasapi_loopback_capture.h"
#include "wasapi_native.h"
#include "wasapi_mmcss_scope.h"
#include "wasapi_reconnect.h"
#include "wasapi_drain.h"
#include <avrt.h>
#include "wasapi_audio_pipeline.h"
#include "wasapi_buffered_sink.h"
#include "wasapi_silence_policy.h"
#include "wasapi_packet_diagnostics.h"
#include <QDebug>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <type_traits>
#include <cmath>
extern "C" {
#include <libavutil/time.h>
}

namespace capture::wasapi {
using namespace native;
namespace {
struct MmcssApi {
    using Handle = HANDLE;
    using TaskIndex = DWORD;
    Handle registerTask(const wchar_t *name, TaskIndex *index) noexcept {
        return AvSetMmThreadCharacteristicsW(name, index);
    }
    bool revert(Handle handle) noexcept { return AvRevertMmThreadCharacteristics(handle) != FALSE; }
    DWORD lastError() noexcept { return GetLastError(); }
    void logError(const char *operation, DWORD error) noexcept {
        qWarning() << "[WASAPI] MMCSS" << operation << "failed, Win32 error=" << error
                   << "(nonfatal)";
    }
};
ERole roleOf(EndpointRole role) {
    return role == EndpointRole::Console ? eConsole :
           role == EndpointRole::Multimedia ? eMultimedia : eCommunications;
}
QString message(HRESULT hr) {
    wchar_t *buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, hr, 0,
                   reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    QString result = buffer ? QString::fromWCharArray(buffer).trimmed()
                            : QStringLiteral("HRESULT 0x%1").arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
    LocalFree(buffer);
    return result;
}
CaptureStartResult failure(HRESULT hr, const QString &op, CaptureErrorStage stage) {
    CaptureError error;
    error.backend = CaptureBackend::Wasapi;
    error.role = CaptureDeviceKind::SystemAudio;
    error.stage = stage;
    error.ffmpegError = static_cast<int>(hr);
    error.ffmpegMessage = message(hr);
    error.userMessage = op + QStringLiteral(": ") + error.ffmpegMessage;
    error.remediation = QStringLiteral("Check the playback device; use a microphone or install virtual-audio-capturer.");
    return CaptureStartResult::failure(std::move(error));
}
}
struct LoopbackCapture::Impl {
    LoopbackConfig config;
    std::shared_ptr<WorkerLifetime::Signals> lifetime;
    std::function<void(const LoopbackRuntimeError &)> runtimeError;
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> abort{false}, stop{false}, go{false};
    std::atomic<LoopbackState> state{LoopbackState::Idle};
    // Handle access is serialized with creation/destruction on the worker.
    HANDLE wake = nullptr;
    CaptureStartResult result;
    MixFormat mix;
    LoopbackStats statistics; // capture-thread owned
    mutable std::mutex statsMutex;
    LoopbackStats publishedStatistics;
    bool finishedLogged = false; // control thread, after joining both workers
    PacketDiagnostics packetDiagnostics;
    std::atomic<int64_t> startUs{0};
    int64_t startQpc = 0;
    std::shared_ptr<AudioFrameSink> sink;
    explicit Impl(LoopbackConfig c) : config(std::move(c)) {}
    void signal() { std::lock_guard<std::mutex> lock(mutex); if (wake) SetEvent(wake); cv.notify_all(); }
    void publish(CaptureStartResult r) {
        std::lock_guard<std::mutex> lock(mutex);
        result = std::move(r);
        state = result.ok ? LoopbackState::Prepared : LoopbackState::Failed;
        lifetime->prepared();
    }
    std::unique_ptr<Stream> openStream(IMMDeviceEnumerator *enumerator, HANDLE event,
                                       AudioPipeline &pipeline) {
        auto stream = std::make_unique<Stream>();
        check(config.endpointId.empty()
              ? enumerator->GetDefaultAudioEndpoint(eRender, roleOf(config.role), stream->device.put())
              : enumerator->GetDevice(config.endpointId.c_str(), stream->device.put()), "Playback endpoint");
        auto activate = [&] {
            check(stream->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                  reinterpret_cast<void **>(stream->client.put())), "Activate");
        };
        activate();
        check(stream->client->GetMixFormat(&stream->format.p), "GetMixFormat");
        const auto *wave = stream->format.p;
        MixFormat info;
        info.sampleRate = wave->nSamplesPerSec; info.channels = wave->nChannels;
        info.bitsPerSample = info.validBitsPerSample = wave->wBitsPerSample;
        info.isFloat = wave->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        bool pcm = wave->wFormatTag == WAVE_FORMAT_PCM;
        if (wave->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wave->cbSize >= 22) {
            auto *ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(wave);
            info.channelMask = ext->dwChannelMask;
            info.validBitsPerSample = ext->Samples.wValidBitsPerSample;
            info.isFloat = ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            pcm = ext->SubFormat == KSDATAFORMAT_SUBTYPE_PCM;
        }
        qInfo() << "[WASAPI] mix" << wave->wFormatTag << wave->cbSize << info.sampleRate
                << info.channels << info.bitsPerSample << info.validBitsPerSample
                << info.channelMask << info.isFloat << wave->nBlockAlign << wave->nAvgBytesPerSec;
        AVSampleFormat format = AV_SAMPLE_FMT_NONE;
        if (info.isFloat) format = info.bitsPerSample == 32 ? AV_SAMPLE_FMT_FLT :
                                  info.bitsPerSample == 64 ? AV_SAMPLE_FMT_DBL : AV_SAMPLE_FMT_NONE;
        if (pcm) format = info.bitsPerSample == 8 ? AV_SAMPLE_FMT_U8 :
                          info.bitsPerSample == 16 ? AV_SAMPLE_FMT_S16 :
                          (info.bitsPerSample == 24 || info.bitsPerSample == 32) ? AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_NONE;
        if (format == AV_SAMPLE_FMT_NONE || !info.channels || !info.sampleRate ||
            wave->nBlockAlign != info.channels * info.bitsPerSample / 8)
            throw std::invalid_argument("Unsupported WASAPI mix format");
        AVChannelLayout layout{};
        if (info.channelMask) av_channel_layout_from_mask(&layout, info.channelMask);
        else av_channel_layout_default(&layout, info.channels);
        if (layout.nb_channels != info.channels) { av_channel_layout_uninit(&layout); throw std::invalid_argument("Invalid channel mask"); }
        media::AudioFormat input(info.sampleRate, format, layout, {1, info.sampleRate});
        av_channel_layout_uninit(&layout);
        pipeline.configure(input);
        DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK;
        HRESULT hr = E_FAIL;
        if (config.preferEventCallback) {
            hr = stream->client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                    config.bufferDuration.count() * 10000, 0, wave, nullptr);
            if (SUCCEEDED(hr)) hr = stream->client->SetEventHandle(event);
        }
        if (FAILED(hr)) {
            activate();
            check(stream->client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags,
                  config.bufferDuration.count() * 10000, 0, wave, nullptr), "Initialize polling");
            qInfo() << "[WASAPI] polling mode";
        } else qInfo() << "[WASAPI] event mode (5 ms watchdog for idle/legacy Windows)";
        check(stream->client->GetService(__uuidof(IAudioCaptureClient),
              reinterpret_cast<void **>(stream->capture.put())), "GetService");
        { std::lock_guard<std::mutex> lock(mutex); mix = info; }
        return stream;
    }
    void updateStatistics(const AudioPipeline &pipeline) {
        const auto sinkStats = sink->diagnostics();
        statistics.outputFrames = pipeline.samples();
        statistics.generatedSilenceFrames = pipeline.generatedSilenceSamples();
        statistics.trimmedSamples = pipeline.trimmedSamples();
        statistics.discontinuities = packetDiagnostics.discontinuities;
        statistics.estimatedLostSamples = packetDiagnostics.estimatedLostSamples;
        statistics.unquantifiedDiscontinuities = packetDiagnostics.unquantifiedDiscontinuities;
        statistics.sinkBlockedMs = sinkStats.blockedNs / 1000000.0;
        statistics.sinkBlockedMaxMs = sinkStats.maxBlockedNs / 1000000.0;
        statistics.rejectedSamples = sinkStats.rejectedSamples;
        // Observers cannot stall live device capture. A contended snapshot is
        // simply skipped; joinFor publishes the final one on the control thread.
        std::unique_lock<std::mutex> lock(statsMutex, std::try_to_lock);
        if (lock.owns_lock()) publishedStatistics = statistics;
    }
    void run() {
        bool opened = false;
        try {
            MmcssApi mmcssApi;
            MmcssScope<MmcssApi> mmcss(mmcssApi);
            ComScope com;
            Event event;
            { std::lock_guard<std::mutex> lock(mutex); wake = event.handle; }
            // Clear published handle before Event destruction, including exception paths.
            struct Clear { Impl &d; ~Clear() { std::lock_guard<std::mutex> l(d.mutex); d.wake = nullptr; } } clear{*this};
            ComPtr<IMMDeviceEnumerator> enumerator;
            check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(enumerator.put())), "Enumerator");
            ComPtr<Notification> notification;
            notification.p = new Notification(roleOf(config.role));
            bool registered = config.followDefaultDevice && config.endpointId.empty();
            if (registered) check(enumerator->RegisterEndpointNotificationCallback(notification.p), "Register notification");
            struct Unregister { IMMDeviceEnumerator *e; Notification *n; bool active;
                ~Unregister() { if (active) e->UnregisterEndpointNotificationCallback(n); }
            } registration{enumerator.p, notification.p, registered};
            AudioPipeline pipeline(*sink);
            struct CommitStats {
                Impl &owner; AudioPipeline &pipeline;
                ~CommitStats() { owner.updateStatistics(pipeline); }
            } commit{*this, pipeline};
            auto stream = openStream(enumerator.p, event.handle, pipeline);
            publish(CaptureStartResult::success()); opened = true;
            { std::unique_lock<std::mutex> lock(mutex); cv.wait(lock, [&]{ return go || stop || abort; }); }
            if (!abort && !stop) {
                startQpc = qpc100ns(); startUs = av_gettime_relative();
                pipeline.setSessionStartUs(startUs);
                check(stream->client->Start(), "Start"); stream->started = true; state = LoopbackState::Running;
            }
            SilencePolicy silence(startUs);
            auto fill = [&](bool receivedPacket, bool bufferEmpty) {
                if (!config.fillIdleWithSilence || !startQpc || abort || stop) return;
                const auto target = silence.target(av_gettime_relative(), pipeline.samples(),
                    pipeline.pendingSamples(), receivedPacket, bufferEmpty);
                if (target > pipeline.samples()) {
                    if (!pipeline.idleUntil(target)) abort = true;
                    updateStatistics(pipeline);
                }
            };
            auto drain = [&](bool finalDrain) {
                return drainPackets(
                    [&] { return abort || (!finalDrain && (stop || notification->changed.load())); },
                    [&] {
                        UINT32 size = 0;
                        check(stream->capture->GetNextPacketSize(&size), "GetNextPacketSize");
                        return size;
                    },
                    [&] {
                        BYTE *data = nullptr; UINT32 count = 0; DWORD flags = 0; UINT64 position = 0, qpc = 0;
                        HRESULT hr = stream->capture->GetBuffer(&data, &count, &flags, &position, &qpc);
                        if (hr == AUDCLNT_S_BUFFER_EMPTY) return false;
                        check(hr, "GetBuffer");
                        packetDiagnostics.observe(position, count, mix.sampleRate,
                            flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY,
                            !(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR));
                        {
                            Packet packet{stream->capture.p, count};
                            if (count) {
                                auto frame = media::makeFrame();
                                MixFormat info = mix;
                                frame->format = info.isFloat ? (info.bitsPerSample == 32 ? AV_SAMPLE_FMT_FLT : AV_SAMPLE_FMT_DBL) :
                                    info.bitsPerSample == 8 ? AV_SAMPLE_FMT_U8 : info.bitsPerSample == 16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_S32;
                                frame->sample_rate = info.sampleRate; frame->nb_samples = count; frame->pts = AV_NOPTS_VALUE;
                                if (info.channelMask) av_channel_layout_from_mask(&frame->ch_layout, info.channelMask);
                                else av_channel_layout_default(&frame->ch_layout, info.channels);
                                if (av_frame_get_buffer(frame.get(), 0) < 0) throw std::bad_alloc();
                                if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                                    av_samples_set_silence(frame->extended_data, 0, count, info.channels, static_cast<AVSampleFormat>(frame->format));
                                else if (info.bitsPerSample == 24 && !info.isFloat) {
                                    auto *out = reinterpret_cast<uint32_t *>(frame->data[0]);
                                    for (size_t i = 0; i < size_t(count) * info.channels; ++i)
                                        out[i] = uint32_t(data[i*3]) << 8 | uint32_t(data[i*3+1]) << 16 | uint32_t(data[i*3+2]) << 24;
                                } else memcpy(frame->data[0], data, size_t(count) * stream->format.p->nBlockAlign);
                                if (!(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)) {
                                    int64_t expected = (static_cast<int64_t>(qpc) - startQpc) * kOutputSampleRate / 10000000;
                                    if (std::llabs(expected - pipeline.samples()) > SilencePolicy::kMinimumGapSamples) {
                                        ++statistics.driftWarnings;
                                    }
                                }
                                const int64_t captureUs = (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
                                    ? av_gettime_relative() - int64_t(count) * 1000000 / info.sampleRate
                                    : startUs + (static_cast<int64_t>(qpc) - startQpc) / 10;
                                silence.packet(); // Any real/SILENT packet resets idle evidence.
                                if (!pipeline.packet(*frame, captureUs)) abort = true;
                                updateStatistics(pipeline);
                                ++statistics.packets; statistics.deviceFrames += count;
                                statistics.generatedSilenceFrames = pipeline.generatedSilenceSamples();
                                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) statistics.silentPacketFrames += count;
                                if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) ++statistics.timestampErrors;
                            }
                        }
                        return true;
                    });
            };
            auto retireStream = [&] {
                return stopAndDrain<Error>(
                    [&] {
                        const HRESULT hr = stream->client->Stop();
                        stream->started = false;
                        check(hr, "Stop before drain");
                    },
                    [&] { return drain(true); },
                    [&] { return bool(abort); },
                    [](const Error &error) {
                        return error.code == AUDCLNT_E_DEVICE_INVALIDATED || error.code == AUDCLNT_E_SERVICE_NOT_RUNNING;
                    },
                    [&](const Error &error) {
                        qWarning() << "[WASAPI] old device unavailable while draining:" << error.operation << error.code;
                    });
            };
            while (!abort && !stop) {
                try {
                    if (notification->changed.exchange(false)) {
                        retireStream(); // Read/release old packets before resetting the stream.
                        throw Error{AUDCLNT_E_DEVICE_INVALIDATED, "Default device changed"};
                    }
                    const auto drained = drain(false);
                    fill(drained.receivedPacket, drained.empty);
                } catch (const Error &error) {
                    if (error.code != AUDCLNT_E_DEVICE_INVALIDATED && error.code != AUDCLNT_E_SERVICE_NOT_RUNNING) throw;
                    stream.reset();
                    packetDiagnostics.resetPosition();
                    silence.reset(); // No confirmed empty buffer while the device is absent.
                    const bool recovered = reconnect<Error>(
                        [&] { return abort || stop; },
                        [&] {
                            // Device loss is not proof of silence; do not advance from wall time.
                            for (int tick = 0; tick < 40 && !abort && !stop; ++tick)
                                WaitForSingleObject(event.handle, 5);
                        },
                        [&] {
                            stream = openStream(enumerator.p, event.handle, pipeline);
                            check(stream->client->Start(), "Restart"); stream->started = true;
                            ++statistics.reopenCount;
                        },
                        [&](int attempt, const auto &failure) {
                            stream.reset();
                            if constexpr (std::is_base_of_v<std::exception, std::decay_t<decltype(failure)>>)
                                qWarning() << "[WASAPI] reopen attempt" << attempt << "failed:" << failure.what();
                            else
                                qWarning() << "[WASAPI] reopen attempt" << attempt << "failed:" << failure.code;
                        });
                    if (abort || stop) break;
                    if (!recovered) throw error;
                    if (runtimeError) runtimeError({error.code, QString::fromLatin1(error.operation), message(error.code), true});
                }
                updateStatistics(pipeline);
                WaitForSingleObject(event.handle, 5);
            }
            state = LoopbackState::Stopping;
            int64_t tailTarget = 0;
            if (stream && stream->started) {
                // Qualify silence using only time elapsed while capture was running.
                // Do not wait on a stopped device to manufacture an idle interval.
                const int64_t stoppedAtUs = av_gettime_relative();
                const auto drained = retireStream();
                if (!abort && config.fillIdleWithSilence && startQpc)
                    tailTarget = silence.target(stoppedAtUs, pipeline.samples(), pipeline.pendingSamples(),
                                                drained.receivedPacket, drained.empty);
            }
            pipeline.stop(abort, tailTarget, startUs);
            updateStatistics(pipeline);
            state = LoopbackState::Finished;
        } catch (const Error &error) {
            if (!opened) publish(failure(error.code, QString::fromLatin1(error.operation), CaptureErrorStage::InputOpen));
            else runtimeFailure(error.code, QString::fromLatin1(error.operation));
        } catch (const std::exception &error) {
            if (!opened) publish(failure(E_INVALIDARG, QString::fromUtf8(error.what()), CaptureErrorStage::InputFormat));
            else runtimeFailure(E_FAIL, QString::fromUtf8(error.what()));
        } catch (...) {
            if (!opened) publish(failure(E_FAIL, QStringLiteral("unexpected capture error"), CaptureErrorStage::InputOpen));
            else runtimeFailure(E_FAIL, QStringLiteral("unexpected capture error"));
        }
        // Also terminate the delivery worker if opening/configuration failed.
        sink->finish();
    }
    void logFinished() {
        if (finishedLogged) return;
        finishedLogged = true;
        const auto &s = statistics; // capture task has joined
        const auto delivery = sink->diagnostics();
        qInfo() << "[WASAPI] finished samples=" << s.outputFrames
                << "generated_silence_samples=" << s.generatedSilenceFrames
                << "data_discontinuities=" << s.discontinuities
                << "lost_samples_48k_estimate=" << s.estimatedLostSamples
                << "unquantified_discontinuities=" << s.unquantifiedDiscontinuities
                << "trimmed_samples_48k=" << s.trimmedSamples << "sink_rejected_samples_48k=" << delivery.rejectedSamples
                << "enqueue_blocked_total_ms=" << delivery.blockedNs / 1000000.0
                << "enqueue_blocked_max_ms=" << delivery.maxBlockedNs / 1000000.0
                << "downstream_dropped_samples_48k=" << delivery.downstreamDroppedSamples
                << "downstream_dropped_frames=" << delivery.downstreamDroppedFrames
                << "downstream_silence_samples_48k=" << delivery.downstreamSilenceSamples
                << "delivery_blocked_total_ms=" << delivery.deliveryBlockedNs / 1000000.0
                << "delivery_blocked_max_ms=" << delivery.deliveryMaxBlockedNs / 1000000.0
                << "abort=" << bool(abort);
    }
    void runtimeFailure(HRESULT hr, const QString &operation) {
        state = LoopbackState::Failed;
        if (!abort) {
            sink->finish();
            if (runtimeError) runtimeError({hr, operation, message(hr), false});
        }
    }
};
LoopbackCapture::LoopbackCapture(LoopbackConfig c) : d(std::make_shared<Impl>(std::move(c))) {
    d->lifetime = worker_.completion();
}
LoopbackCapture::~LoopbackCapture() { requestAbort(); join(); }
CaptureStartResult LoopbackCapture::open(std::shared_ptr<AudioFrameSink> sink, std::chrono::milliseconds timeout) {
    if (!sink || worker_.started() || worker_.completion()->timedOut())
        return failure(E_INVALIDARG, QStringLiteral("open (single-use instance)"), CaptureErrorStage::Configuration);
    d->sink = std::make_shared<BufferedAudioSink>(std::move(sink),
        BufferedAudioSink::kCapacityFrames, BufferedAudioSink::kCapacitySamples,
        [] { qInfo() << "[WASAPI] started"; }); // delivery thread, preserves PC2 probe marker
    d->runtimeError = onRuntimeError;
    worker_.start([impl = d] { impl->run(); });
    if (!worker_.waitPrepared(timeout)) {
        abandon();
        qWarning() << "[WASAPI] open timeout; aborted and detached, state/sink retained until driver returns";
        return failure(HRESULT_FROM_WIN32(WAIT_TIMEOUT), QStringLiteral("open timeout"), CaptureErrorStage::InputOpen);
    }
    std::lock_guard<std::mutex> lock(d->mutex);
    return d->result;
}
void LoopbackCapture::start() { if (!d->abort && !worker_.completion()->timedOut()) { d->go = true; d->signal(); } }
void LoopbackCapture::requestDrainStop() { d->stop = true; d->signal(); }
void LoopbackCapture::requestAbort() {
    d->abort = true;
    if (d->sink) d->sink->cancel();
    d->signal();
}
bool LoopbackCapture::joinFor(std::chrono::milliseconds timeout) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    if (!worker_.joinFor(timeout)) return false;
    const auto remaining = std::max(std::chrono::milliseconds(0),
        std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now()));
    if (d->sink && !d->sink->joinFor(remaining)) return false;
    { std::lock_guard<std::mutex> lock(d->statsMutex); d->publishedStatistics = d->statistics; }
    if (d->sink) d->logFinished();
    return true;
}
void LoopbackCapture::abandon() { requestAbort(); worker_.abandon(); }
void LoopbackCapture::join() {
    if (!joinFor(std::chrono::milliseconds(500))) {
        qWarning() << "[WASAPI] join deadline exceeded; detached worker will reclaim its own resources";
        abandon();
    }
}
LoopbackState LoopbackCapture::state() const {
    return worker_.completion()->timedOut() ? LoopbackState::Failed : d->state.load();
}
MixFormat LoopbackCapture::mixFormat() const { std::lock_guard<std::mutex> lock(d->mutex); return d->mix; }
LoopbackStats LoopbackCapture::stats() const {
    LoopbackStats result;
    { std::lock_guard<std::mutex> lock(d->statsMutex); result = d->publishedStatistics; }
    if (d->sink) {
        const auto s = d->sink->diagnostics();
        result.downstreamDroppedSamples = s.downstreamDroppedSamples;
        result.downstreamDroppedFrames = s.downstreamDroppedFrames;
        result.downstreamSilenceSamples = s.downstreamSilenceSamples;
        result.deliveryBlockedMs = s.deliveryBlockedNs / 1000000.0;
        result.deliveryBlockedMaxMs = s.deliveryMaxBlockedNs / 1000000.0;
        result.rejectedSamples = s.rejectedSamples;
    }
    return result;
}
int64_t LoopbackCapture::startQpcUs() const { return d->startUs; }
std::vector<EndpointInfo> LoopbackCapture::enumerateEndpoints() {
    std::vector<EndpointInfo> result;
    // Enumeration also owns its COM apartment on a dedicated thread.
    std::thread worker([&] {
        try {
            ComScope com; ComPtr<IMMDeviceEnumerator> e;
            check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(e.put())), "Enumerator");
            ComPtr<IMMDevice> def; TaskMemory<wchar_t> defaultId;
            if (SUCCEEDED(e->GetDefaultAudioEndpoint(eRender, eConsole, def.put()))) def->GetId(&defaultId.p);
            ComPtr<IMMDeviceCollection> devices;
            check(e->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, devices.put()), "Endpoints");
            UINT count = 0; check(devices->GetCount(&count), "Count");
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IMMDevice> device; TaskMemory<wchar_t> id;
                check(devices->Item(i, device.put()), "Item"); check(device->GetId(&id.p), "Id");
                EndpointInfo info; info.id = id.p; info.isDefault = defaultId.p && info.id == defaultId.p;
                ComPtr<IPropertyStore> properties;
                if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, properties.put()))) {
                    PROPVARIANT value; PropVariantInit(&value);
                    if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR)
                        info.friendlyName = QString::fromWCharArray(value.pwszVal);
                    PropVariantClear(&value);
                }
                result.push_back(std::move(info));
            }
        } catch (...) { result.clear(); }
    });
    worker.join(); return result;
}
} // namespace

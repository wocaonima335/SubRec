// Instantiate SDK property keys (notably PKEY_Device_FriendlyName) here:
// MinGW's uuid/propsys libraries do not supply them. KS format GUIDs use
// DEFINE_GUIDEX instead and are supplied by capture_module's ksuser dependency.
// Keep this before any header that could transitively include the Windows SDK;
// do not put INITGUID in the shared wasapi_native.h header.
#include <initguid.h>

#include "wasapi_loopback_capture.h"
#include "wasapi_native.h"
#include "wasapi_audio_pipeline.h"
#include <QDebug>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cmath>
extern "C" {
#include <libavutil/time.h>
}

namespace capture::wasapi {
using namespace native;
namespace {
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
    LoopbackCapture &owner;
    LoopbackConfig config;
    std::thread worker;
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> abort{false}, stop{false}, go{false};
    std::atomic<LoopbackState> state{LoopbackState::Idle};
    // Handle access is serialized with creation/destruction on the worker.
    HANDLE wake = nullptr;
    bool prepared = false;
    CaptureStartResult result;
    MixFormat mix;
    LoopbackStats statistics;
    std::atomic<int64_t> startUs{0};
    int64_t startQpc = 0;
    AudioFrameSink *sink = nullptr;
    Impl(LoopbackCapture &o, LoopbackConfig c) : owner(o), config(std::move(c)) {}
    void signal() { std::lock_guard<std::mutex> lock(mutex); if (wake) SetEvent(wake); cv.notify_all(); }
    void publish(CaptureStartResult r) {
        std::lock_guard<std::mutex> lock(mutex);
        result = std::move(r); prepared = true;
        state = result.ok ? LoopbackState::Prepared : LoopbackState::Failed;
        cv.notify_all();
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
    void run() {
        bool opened = false;
        try {
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
            auto stream = openStream(enumerator.p, event.handle, pipeline);
            publish(CaptureStartResult::success()); opened = true;
            { std::unique_lock<std::mutex> lock(mutex); cv.wait(lock, [&]{ return go || stop || abort; }); }
            if (!abort && !stop) {
                startQpc = qpc100ns(); startUs = av_gettime_relative();
                check(stream->client->Start(), "Start"); stream->started = true; state = LoopbackState::Running;
                qInfo() << "[WASAPI] started";
            }
            bool first = true;
            auto fill = [&] {
                if (!config.fillIdleWithSilence || !startQpc || abort) return;
                auto target = (qpc100ns() - startQpc) * 48000 / 10000000;
                if (target - pipeline.samples() > 960) {
                    auto before = pipeline.samples();
                    if (!pipeline.silenceUntil(target)) abort = true;
                    std::lock_guard<std::mutex> lock(mutex);
                    statistics.generatedSilenceFrames += pipeline.samples() - before;
                }
            };
            while (!abort && !stop) {
                try {
                    if (notification->changed.exchange(false)) throw Error{AUDCLNT_E_DEVICE_INVALIDATED, "Default device changed"};
                    UINT32 size = 0;
                    check(stream->capture->GetNextPacketSize(&size), "GetNextPacketSize");
                    while (size && !abort && !stop) {
                        BYTE *data = nullptr; UINT32 count = 0; DWORD flags = 0; UINT64 position = 0, qpc = 0;
                        HRESULT hr = stream->capture->GetBuffer(&data, &count, &flags, &position, &qpc);
                        if (hr == AUDCLNT_S_BUFFER_EMPTY) break;
                        check(hr, "GetBuffer");
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
                                    int64_t expected = (static_cast<int64_t>(qpc) - startQpc) * 48000 / 10000000;
                                    if (first || std::llabs(expected - pipeline.samples()) > 4800)
                                        qInfo() << "[WASAPI] packet clock delta samples" << expected - pipeline.samples() << "first" << first;
                                    if (std::llabs(expected - pipeline.samples()) > 4800) {
                                        std::lock_guard<std::mutex> lock(mutex); ++statistics.driftWarnings;
                                    }
                                    first = false;
                                }
                                auto before = av_gettime_relative();
                                if (!pipeline.packet(*frame)) abort = true;
                                std::lock_guard<std::mutex> lock(mutex);
                                ++statistics.packets; statistics.deviceFrames += count;
                                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) statistics.silentPacketFrames += count;
                                if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ++statistics.discontinuities;
                                if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) ++statistics.timestampErrors;
                                statistics.sinkBlockedMs += (av_gettime_relative() - before) / 1000;
                            }
                        }
                        check(stream->capture->GetNextPacketSize(&size), "GetNextPacketSize");
                    }
                    fill();
                } catch (const Error &error) {
                    if (error.code != AUDCLNT_E_DEVICE_INVALIDATED && error.code != AUDCLNT_E_SERVICE_NOT_RUNNING) throw;
                    stream.reset();
                    bool recovered = false;
                    for (int retry = 0; retry < 10 && !abort && !stop; ++retry) {
                        // Keep the audio master clock moving throughout the retry delay.
                        for (int tick = 0; tick < 40 && !abort && !stop; ++tick) { fill(); WaitForSingleObject(event.handle, 5); }
                        if (abort || stop) break;
                        try {
                            stream = openStream(enumerator.p, event.handle, pipeline);
                            check(stream->client->Start(), "Restart"); stream->started = true;
                            recovered = true;
                            { std::lock_guard<std::mutex> lock(mutex); ++statistics.reopenCount; }
                            break;
                        } catch (const Error &) { stream.reset(); }
                    }
                    if (abort || stop) break;
                    if (!recovered) throw error;
                    if (owner.onRuntimeError) owner.onRuntimeError({error.code, QString::fromLatin1(error.operation), message(error.code), true});
                }
                { std::lock_guard<std::mutex> lock(mutex); statistics.outputFrames = pipeline.samples(); }
                WaitForSingleObject(event.handle, 5);
            }
            if (stream && stream->started) { stream->client->Stop(); stream->started = false; }
            state = LoopbackState::Stopping;
            pipeline.stop(abort);
            { std::lock_guard<std::mutex> lock(mutex); statistics.outputFrames = pipeline.samples(); }
            qInfo() << "[WASAPI] finished samples" << pipeline.samples()
                    << "generated silence" << statistics.generatedSilenceFrames << "abort" << bool(abort);
            state = LoopbackState::Finished;
        } catch (const Error &error) {
            if (!opened) publish(failure(error.code, QString::fromLatin1(error.operation), CaptureErrorStage::InputOpen));
            else runtimeFailure(error.code, QString::fromLatin1(error.operation));
        } catch (const std::exception &error) {
            if (!opened) publish(failure(E_INVALIDARG, QString::fromUtf8(error.what()), CaptureErrorStage::InputFormat));
            else runtimeFailure(E_FAIL, QString::fromUtf8(error.what()));
        }
    }
    void runtimeFailure(HRESULT hr, const QString &operation) {
        state = LoopbackState::Failed;
        if (!abort) {
            sink->finish();
            if (owner.onRuntimeError) owner.onRuntimeError({hr, operation, message(hr), false});
        }
    }
};
LoopbackCapture::LoopbackCapture(LoopbackConfig c) : d(std::make_unique<Impl>(*this, std::move(c))) {}
LoopbackCapture::~LoopbackCapture() { requestAbort(); join(); }
CaptureStartResult LoopbackCapture::open(AudioFrameSink *sink, std::chrono::milliseconds timeout) {
    if (!sink || d->worker.joinable()) return failure(E_INVALIDARG, QStringLiteral("open"), CaptureErrorStage::Configuration);
    d->sink = sink; d->abort = false; d->stop = false; d->go = false;
    d->prepared = false; d->statistics = {}; d->startQpc = 0; d->startUs = 0;
    d->worker = std::thread([this] { d->run(); });
    std::unique_lock<std::mutex> lock(d->mutex);
    if (!d->cv.wait_for(lock, timeout, [this]{ return d->prepared; })) {
        lock.unlock(); requestAbort();
        return failure(HRESULT_FROM_WIN32(WAIT_TIMEOUT), QStringLiteral("open timeout"), CaptureErrorStage::InputOpen);
    }
    return d->result;
}
void LoopbackCapture::start() { d->go = true; d->signal(); }
void LoopbackCapture::requestDrainStop() { d->stop = true; d->signal(); }
void LoopbackCapture::requestAbort() { d->abort = true; d->signal(); }
void LoopbackCapture::join() { if (d->worker.joinable()) d->worker.join(); d->state = LoopbackState::Idle; }
LoopbackState LoopbackCapture::state() const { return d->state; }
MixFormat LoopbackCapture::mixFormat() const { std::lock_guard<std::mutex> lock(d->mutex); return d->mix; }
LoopbackStats LoopbackCapture::stats() const { std::lock_guard<std::mutex> lock(d->mutex); return d->statistics; }
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

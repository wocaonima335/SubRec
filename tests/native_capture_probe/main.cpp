#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
// MinGW's uuid import library does not define the friendly-name property key.
#include <initguid.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <ks.h>
#include <ksmedia.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
constexpr uint64_t hashBasis = 14695981039346656037ull;
constexpr uint64_t hashPrime = 1099511628211ull;

std::string hex(uint64_t value, unsigned width = 8) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setfill('0') << std::setw(width) << value;
    return out.str();
}

void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        throw std::runtime_error(std::string(operation) + " failed HRESULT=" +
                                 hex(static_cast<uint32_t>(hr)));
    }
}

std::string utf8(const wchar_t* value) {
    if (!value || !*value) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                                         nullptr, 0, nullptr, nullptr);
    if (count == 0) throw std::runtime_error("WideCharToMultiByte sizing failed");
    std::string result(static_cast<size_t>(count), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                            result.data(), count, nullptr, nullptr)) {
        throw std::runtime_error("WideCharToMultiByte conversion failed");
    }
    result.pop_back();
    return result;
}

std::string guidText(const GUID& guid) {
    wchar_t text[40]{};
    if (!StringFromGUID2(guid, text, 40)) return "<unavailable>";
    return utf8(text);
}

template<class T> class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { if (ptr_) ptr_->Release(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)) {}
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            if (ptr_) ptr_->Release();
            ptr_ = std::exchange(other.ptr_, nullptr);
        }
        return *this;
    }
    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    T** put() {
        if (ptr_) throw std::logic_error("ComPtr output must be empty");
        return &ptr_;
    }
private:
    T* ptr_ = nullptr;
};

class ComApartment {
public:
    ComApartment() { check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx(MTA)"); }
    ~ComApartment() { CoUninitialize(); }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
};

template<class T> struct TaskMemory {
    T* ptr = nullptr;
    TaskMemory() = default;
    ~TaskMemory() { CoTaskMemFree(ptr); }
    TaskMemory(const TaskMemory&) = delete;
    TaskMemory& operator=(const TaskMemory&) = delete;
};

struct PropertyValue {
    PROPVARIANT value;
    PropertyValue() { PropVariantInit(&value); }
    ~PropertyValue() { PropVariantClear(&value); }
    PropertyValue(const PropertyValue&) = delete;
    PropertyValue& operator=(const PropertyValue&) = delete;
};

void hashBytes(uint64_t& hash, const BYTE* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        hash ^= data ? data[i] : 0; // Silent WASAPI buffers may have no backing data.
        hash *= hashPrime;
    }
}

struct Options {
    std::string mode;
    unsigned seconds = 3;
    bool listOnly = false;
    bool outputSpecified = false;
    unsigned output = 0;
};

void usage() {
    std::cout << "Usage: native_capture_probe <wasapi|dxgi> [--seconds 1..60] [--list]"
                 " [--output INDEX]\n"
                 "  WASAPI: active render endpoints; capture default eConsole loopback.\n"
                 "  DXGI: enumerate every adapter/output; --output uses the printed global index.\n"
                 "  Without --output choose first attached desktop output.\n"
                 "  --list enumerates only; --output is valid only in dxgi mode.\n"
                 "  Exit: 0 sampled/listed, 1 API/runtime error, 2 invalid CLI, 3 no usable data.\n";
}

unsigned number(const std::string& text) {
    if (text.empty()) throw std::invalid_argument("empty numeric argument");
    uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') throw std::invalid_argument("expected unsigned decimal integer");
        value = value * 10 + static_cast<unsigned>(c - '0');
        if (value > std::numeric_limits<unsigned>::max()) {
            throw std::invalid_argument("integer out of range");
        }
    }
    return static_cast<unsigned>(value);
}

Options parse(int argc, char** argv) {
    if (argc < 2) throw std::invalid_argument("missing mode");
    Options options;
    options.mode = argv[1];
    if (options.mode != "wasapi" && options.mode != "dxgi") {
        throw std::invalid_argument("mode must be wasapi or dxgi");
    }
    bool secondsSpecified = false;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--list" && !options.listOnly) {
            options.listOnly = true;
        } else if (arg == "--seconds" && !secondsSpecified && i + 1 < argc) {
            secondsSpecified = true;
            options.seconds = number(argv[++i]);
            if (options.seconds < 1 || options.seconds > 60) {
                throw std::invalid_argument("--seconds must be in [1,60]");
            }
        } else if (arg == "--output" && !options.outputSpecified && i + 1 < argc) {
            options.outputSpecified = true;
            options.output = number(argv[++i]);
        } else {
            throw std::invalid_argument("unknown, duplicate or incomplete option: " + arg);
        }
    }
    if (options.outputSpecified && options.mode != "dxgi") {
        throw std::invalid_argument("--output is only supported for dxgi");
    }
    return options;
}

std::string deviceId(IMMDevice* device) {
    TaskMemory<wchar_t> id;
    check(device->GetId(&id.ptr), "IMMDevice::GetId");
    return utf8(id.ptr);
}

void logEndpoint(IMMDevice* device, const std::string& prefix) {
    DWORD state = 0;
    check(device->GetState(&state), "IMMDevice::GetState");
    ComPtr<IPropertyStore> properties;
    check(device->OpenPropertyStore(STGM_READ, properties.put()), "OpenPropertyStore");
    PropertyValue name;
    const HRESULT hr = properties->GetValue(PKEY_Device_FriendlyName, &name.value);
    std::cout << prefix << " id=" << std::quoted(deviceId(device)) << " state=" << hex(state);
    if (SUCCEEDED(hr) && name.value.vt == VT_LPWSTR) {
        std::cout << " name=" << std::quoted(utf8(name.value.pwszVal));
    } else {
        std::cout << " name=<unavailable> name_hr=" << hex(static_cast<uint32_t>(hr));
    }
    std::cout << '\n';
}

struct AudioStats {
    uint64_t packets = 0, frames = 0, sampleValues = 0, bytes = 0;
    uint64_t silentPackets = 0, silentFrames = 0, discontinuities = 0, timestampErrors = 0;
    uint64_t bufferGets = 0, bufferReleases = 0, emptyPolls = 0, hash = hashBasis;
    Clock::time_point start = Clock::now();
    bool complete = false;
    ~AudioStats() {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        std::cout << "WASAPI_SUMMARY complete=" << complete << " elapsed_ms=" << ms
                  << " packets=" << packets << " sample_frames=" << frames
                  << " interleaved_sample_values=" << sampleValues << " logical_bytes=" << bytes
                  << " silent_packets=" << silentPackets << " silent_frames=" << silentFrames
                  << " discontinuities=" << discontinuities << " timestamp_errors=" << timestampErrors
                  << " empty_polls=" << emptyPolls << " buffer_gets=" << bufferGets
                  << " buffer_releases=" << bufferReleases << " fnv1a64=" << hex(hash, 16) << '\n';
    }
};

class AudioStream {
public:
    explicit AudioStream(IAudioClient* client) : client_(client) {
        check(client_->Start(), "IAudioClient::Start");
        active_ = true;
    }
    ~AudioStream() {
        if (active_) {
            const HRESULT hr = client_->Stop();
            if (FAILED(hr)) std::cerr << "cleanup IAudioClient::Stop HRESULT=" << hex(static_cast<uint32_t>(hr)) << '\n';
        }
    }
    void stop() {
        const HRESULT hr = client_->Stop();
        if (SUCCEEDED(hr)) active_ = false;
        check(hr, "IAudioClient::Stop");
    }
    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;
private:
    IAudioClient* client_;
    bool active_ = false;
};

class AudioPacket {
public:
    AudioPacket(IAudioCaptureClient* client, UINT32 frames, AudioStats& stats)
        : client_(client), frames_(frames), stats_(stats) { ++stats_.bufferGets; }
    ~AudioPacket() {
        if (active_) {
            const HRESULT hr = releaseImpl();
            if (FAILED(hr)) std::cerr << "cleanup ReleaseBuffer HRESULT=" << hex(static_cast<uint32_t>(hr)) << '\n';
        }
    }
    void release() { check(releaseImpl(), "IAudioCaptureClient::ReleaseBuffer"); }
    AudioPacket(const AudioPacket&) = delete;
    AudioPacket& operator=(const AudioPacket&) = delete;
private:
    HRESULT releaseImpl() {
        active_ = false; // Exactly one release attempt for each acquired packet.
        const HRESULT hr = client_->ReleaseBuffer(frames_);
        if (SUCCEEDED(hr)) ++stats_.bufferReleases;
        return hr;
    }
    IAudioCaptureClient* client_;
    UINT32 frames_;
    AudioStats& stats_;
    bool active_ = true;
};

int runWasapi(const Options& options) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                           __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.put())),
          "CoCreateInstance(MMDeviceEnumerator)");
    ComPtr<IMMDeviceCollection> endpoints;
    check(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, endpoints.put()), "EnumAudioEndpoints");
    UINT count = 0;
    check(endpoints->GetCount(&count), "IMMDeviceCollection::GetCount");
    std::cout << "WASAPI active_render_endpoints=" << count << " default_role=eConsole\n";
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> endpoint;
        check(endpoints->Item(i, endpoint.put()), "IMMDeviceCollection::Item");
        logEndpoint(endpoint.get(), "ENDPOINT index=" + std::to_string(i));
    }
    if (options.listOnly) return 0;
    AudioStats stats; // Also report zero samples if endpoint/format initialization fails.
    ComPtr<IMMDevice> endpoint;
    check(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, endpoint.put()), "GetDefaultAudioEndpoint");
    logEndpoint(endpoint.get(), "SELECTED_ENDPOINT");
    ComPtr<IAudioClient> audio;
    check(endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                             reinterpret_cast<void**>(audio.put())), "Activate(IAudioClient)");
    TaskMemory<WAVEFORMATEX> format;
    check(audio->GetMixFormat(&format.ptr), "IAudioClient::GetMixFormat");
    if (!format.ptr) throw std::runtime_error("GetMixFormat returned null");
    const WAVEFORMATEX& mix = *format.ptr;
    std::cout << "MIX_FORMAT tag=" << hex(mix.wFormatTag, 4) << " channels=" << mix.nChannels
              << " sample_rate=" << mix.nSamplesPerSec << " bits_per_sample=" << mix.wBitsPerSample
              << " block_align=" << mix.nBlockAlign << " avg_bytes_per_sec=" << mix.nAvgBytesPerSec
              << " extra_bytes=" << mix.cbSize;
    if (mix.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        if (mix.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
            throw std::runtime_error("truncated WAVEFORMATEXTENSIBLE");
        }
        const auto& ext = *reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format.ptr);
        std::cout << " valid_bits=" << ext.Samples.wValidBitsPerSample
                  << " channel_mask=" << hex(ext.dwChannelMask)
                  << " subformat=" << guidText(ext.SubFormat);
    }
    std::cout << '\n';
    if (!mix.nBlockAlign || !mix.nChannels || !mix.nSamplesPerSec) {
        throw std::runtime_error("invalid mix format dimensions");
    }
    check(audio->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                            1000000, 0, format.ptr, nullptr), "IAudioClient::Initialize(loopback)");
    UINT32 bufferFrames = 0;
    REFERENCE_TIME defaultPeriod = 0, minimumPeriod = 0;
    check(audio->GetBufferSize(&bufferFrames), "IAudioClient::GetBufferSize");
    check(audio->GetDevicePeriod(&defaultPeriod, &minimumPeriod), "IAudioClient::GetDevicePeriod");
    std::cout << "AUDIO_BUFFER frames=" << bufferFrames << " default_period_100ns=" << defaultPeriod
              << " minimum_period_100ns=" << minimumPeriod << " polling_ms=5\n";
    ComPtr<IAudioCaptureClient> capture;
    check(audio->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(capture.put())),
          "GetService(IAudioCaptureClient)");
    AudioStream stream(audio.get());
    stats.start = Clock::now();
    const auto deadline = stats.start + std::chrono::seconds(options.seconds);
    while (Clock::now() < deadline) {
        UINT32 next = 0;
        check(capture->GetNextPacketSize(&next), "GetNextPacketSize");
        if (!next) {
            ++stats.emptyPolls;
            Sleep(5);
            continue;
        }
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        UINT64 devicePosition = 0, qpcPosition = 0;
        const HRESULT hr = capture->GetBuffer(&data, &frames, &flags, &devicePosition, &qpcPosition);
        if (hr == AUDCLNT_S_BUFFER_EMPTY) {
            ++stats.emptyPolls;
            Sleep(1);
            continue; // No acquired packet and therefore no ReleaseBuffer.
        }
        check(hr, "IAudioCaptureClient::GetBuffer");
        AudioPacket packet(capture.get(), frames, stats);
        const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
        const size_t bytes = static_cast<size_t>(frames) * mix.nBlockAlign;
        if (!silent && bytes && !data) throw std::runtime_error("non-silent packet has null data");
        hashBytes(stats.hash, silent ? nullptr : data, bytes);
        ++stats.packets;
        stats.frames += frames;
        stats.sampleValues += static_cast<uint64_t>(frames) * mix.nChannels;
        stats.bytes += bytes;
        if (silent) { ++stats.silentPackets; stats.silentFrames += frames; }
        if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ++stats.discontinuities;
        if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) ++stats.timestampErrors;
        if (stats.packets == 1) {
            std::cout << "FIRST_PACKET frames=" << frames << " flags=" << hex(flags)
                      << " device_position=" << devicePosition << " qpc_100ns=" << qpcPosition << '\n';
        }
        packet.release();
    }
    stream.stop();
    stats.complete = true;
    if (!stats.frames) {
        std::cerr << "NO_DATA: no WASAPI sample frames; an idle endpoint may produce no packets.\n";
        return 3;
    }
    return 0;
}

struct Output {
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIOutput> output;
    DXGI_OUTPUT_DESC desc{};
    DXGI_ADAPTER_DESC1 adapterDesc{};
    unsigned adapterIndex = 0, localIndex = 0;
};

std::vector<Output> enumerateOutputs() {
    ComPtr<IDXGIFactory1> factory;
    check(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.put())), "CreateDXGIFactory1");
    std::vector<Output> outputs;
    for (UINT a = 0;; ++a) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT hr = factory->EnumAdapters1(a, adapter.put());
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        check(hr, "EnumAdapters1");
        DXGI_ADAPTER_DESC1 adapterDesc{};
        check(adapter->GetDesc1(&adapterDesc), "IDXGIAdapter1::GetDesc1");
        std::cout << "ADAPTER index=" << a << " name=" << std::quoted(utf8(adapterDesc.Description))
                  << " luid=" << hex(static_cast<uint32_t>(adapterDesc.AdapterLuid.HighPart))
                  << ':' << hex(adapterDesc.AdapterLuid.LowPart)
                  << " vendor=" << hex(adapterDesc.VendorId) << " device=" << hex(adapterDesc.DeviceId)
                  << " flags=" << hex(adapterDesc.Flags) << '\n';
        for (UINT o = 0;; ++o) {
            Output entry;
            const HRESULT outputHr = adapter->EnumOutputs(o, entry.output.put());
            if (outputHr == DXGI_ERROR_NOT_FOUND) break;
            check(outputHr, "IDXGIAdapter::EnumOutputs");
            check(entry.output->GetDesc(&entry.desc), "IDXGIOutput::GetDesc");
            adapter->AddRef();
            *entry.adapter.put() = adapter.get();
            entry.adapterDesc = adapterDesc;
            entry.adapterIndex = a;
            entry.localIndex = o;
            const RECT& rect = entry.desc.DesktopCoordinates;
            std::cout << "OUTPUT index=" << outputs.size() << " adapter=" << a << " local_output=" << o
                      << " device=" << std::quoted(utf8(entry.desc.DeviceName))
                      << " attached=" << entry.desc.AttachedToDesktop
                      << " rect=" << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom
                      << " width=" << rect.right - rect.left << " height=" << rect.bottom - rect.top
                      << " rotation=" << static_cast<unsigned>(entry.desc.Rotation) << '\n';
            outputs.push_back(std::move(entry));
        }
    }
    std::cout << "DXGI output_count=" << outputs.size() << '\n';
    return outputs;
}

struct VideoStats {
    uint64_t acquired = 0, released = 0, readable = 0, maps = 0, unmaps = 0;
    uint64_t timeouts = 0, accumulated = 0, changed = 0, bytes = 0, deadlineDrops = 0;
    uint64_t lastHash = hashBasis, streamHash = hashBasis;
    Clock::time_point start = Clock::now();
    bool complete = false;
    ~VideoStats() {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        std::cout << "DXGI_SUMMARY complete=" << complete << " elapsed_ms=" << ms
                  << " acquired_frames=" << acquired << " released_frames=" << released
                  << " readable_frames=" << readable << " maps=" << maps << " unmaps=" << unmaps
                  << " timeouts=" << timeouts << " deadline_dropped_frames=" << deadlineDrops
                  << " accumulated_frames=" << accumulated
                  << " changed_hash_frames=" << changed << " hashed_bytes=" << bytes
                  << " last_fnv1a64=" << hex(lastHash, 16) << " stream_fnv1a64=" << hex(streamHash, 16) << '\n';
    }
};

class DuplicationFrame {
public:
    DuplicationFrame(IDXGIOutputDuplication* duplication, VideoStats& stats)
        : duplication_(duplication), stats_(stats) { ++stats_.acquired; }
    ~DuplicationFrame() {
        if (active_) {
            const HRESULT hr = releaseImpl();
            if (FAILED(hr)) std::cerr << "cleanup ReleaseFrame HRESULT=" << hex(static_cast<uint32_t>(hr)) << '\n';
        }
    }
    void release() { check(releaseImpl(), "IDXGIOutputDuplication::ReleaseFrame"); }
    DuplicationFrame(const DuplicationFrame&) = delete;
    DuplicationFrame& operator=(const DuplicationFrame&) = delete;
private:
    HRESULT releaseImpl() {
        active_ = false;
        const HRESULT hr = duplication_->ReleaseFrame();
        if (SUCCEEDED(hr)) ++stats_.released;
        return hr;
    }
    IDXGIOutputDuplication* duplication_;
    VideoStats& stats_;
    bool active_ = true;
};

class MappedTexture {
public:
    MappedTexture(ID3D11DeviceContext* context, ID3D11Texture2D* texture,
                  const D3D11_MAPPED_SUBRESOURCE& mapped, VideoStats& stats)
        : data(mapped), context_(context), texture_(texture), stats_(stats) { ++stats_.maps; }
    ~MappedTexture() { context_->Unmap(texture_, 0); ++stats_.unmaps; }
    MappedTexture(const MappedTexture&) = delete;
    MappedTexture& operator=(const MappedTexture&) = delete;
    const D3D11_MAPPED_SUBRESOURCE data;
private:
    ID3D11DeviceContext* context_;
    ID3D11Texture2D* texture_;
    VideoStats& stats_;
};

int runDxgi(const Options& options) {
    auto outputs = enumerateOutputs();
    if (options.outputSpecified && options.output >= outputs.size()) {
        throw std::invalid_argument("--output index is not present in this enumeration");
    }
    if (options.listOnly) return 0;
    size_t selected = options.output;
    if (!options.outputSpecified) {
        selected = 0;
        while (selected < outputs.size() && !outputs[selected].desc.AttachedToDesktop) ++selected;
    }
    if (selected >= outputs.size()) throw std::runtime_error("no attached DXGI output");
    auto& output = outputs[selected];
    if (!output.desc.AttachedToDesktop) throw std::runtime_error("selected DXGI output is detached");
    std::cout << "SELECTED_OUTPUT index=" << selected << " adapter=" << output.adapterIndex
              << " local_output=" << output.localIndex
              << " luid=" << hex(static_cast<uint32_t>(output.adapterDesc.AdapterLuid.HighPart))
              << ':' << hex(output.adapterDesc.AdapterLuid.LowPart)
              << " device=" << std::quoted(utf8(output.desc.DeviceName)) << '\n';
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL feature{};
    check(D3D11CreateDevice(output.adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                            device.put(), &feature, context.put()), "D3D11CreateDevice(selected adapter)");
    ComPtr<IDXGIOutput1> output1;
    check(output.output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(output1.put())),
          "QueryInterface(IDXGIOutput1)");
    ComPtr<IDXGIOutputDuplication> duplication;
    check(output1->DuplicateOutput(device.get(), duplication.put()), "IDXGIOutput1::DuplicateOutput");
    DXGI_OUTDUPL_DESC duplicationDesc{};
    duplication->GetDesc(&duplicationDesc);
    std::cout << "DUPLICATION width=" << duplicationDesc.ModeDesc.Width
              << " height=" << duplicationDesc.ModeDesc.Height
              << " format=" << static_cast<unsigned>(duplicationDesc.ModeDesc.Format)
              << " rotation=" << static_cast<unsigned>(duplicationDesc.Rotation)
              << " system_memory=" << duplicationDesc.DesktopImageInSystemMemory
              << " feature_level=" << hex(feature) << '\n';
    ComPtr<ID3D11Texture2D> staging;
    D3D11_TEXTURE2D_DESC stagingDesc{};
    VideoStats stats;
    const auto deadline = stats.start + std::chrono::seconds(options.seconds);
    while (Clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (remaining <= 0) break;
        const UINT wait = static_cast<UINT>(remaining < 100 ? remaining : 100);
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> resource;
        const HRESULT hr = duplication->AcquireNextFrame(wait, &info, resource.put());
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) { ++stats.timeouts; continue; }
        check(hr, "AcquireNextFrame (access lost/session changes require rerun)");
        DuplicationFrame frame(duplication.get(), stats);
        if (!resource.get()) throw std::runtime_error("AcquireNextFrame returned null resource");
        ComPtr<ID3D11Texture2D> texture;
        check(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(texture.put())),
              "QueryInterface(ID3D11Texture2D)");
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if (!desc.Width || !desc.Height || desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
            desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || desc.MipLevels != 1) {
            throw std::runtime_error("unexpected desktop texture; expected single BGRA8 surface");
        }
        if (!staging.get() || desc.Width != stagingDesc.Width || desc.Height != stagingDesc.Height ||
            desc.Format != stagingDesc.Format) {
            stagingDesc = desc;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stagingDesc.MiscFlags = 0;
            ComPtr<ID3D11Texture2D> replacement;
            check(device->CreateTexture2D(&stagingDesc, nullptr, replacement.put()), "CreateTexture2D(staging)");
            staging = std::move(replacement);
        }
        context->CopyResource(staging.get(), texture.get());
        // Poll Map without blocking indefinitely on GPU completion.
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT mapHr = DXGI_ERROR_WAS_STILL_DRAWING;
        while (Clock::now() < deadline) {
            mapHr = context->Map(staging.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
            if (mapHr != DXGI_ERROR_WAS_STILL_DRAWING) break;
            context->Flush();
            Sleep(1);
        }
        if (mapHr == DXGI_ERROR_WAS_STILL_DRAWING) {
            ++stats.deadlineDrops;
            frame.release();
            break; // Normal window end; prior readable frames still constitute success.
        }
        check(mapHr, "ID3D11DeviceContext::Map(staging)");
        {
            MappedTexture mapping(context.get(), staging.get(), mapped, stats);
            const size_t rowBytes = static_cast<size_t>(desc.Width) * 4;
            if (!mapping.data.pData || mapping.data.RowPitch < rowBytes) {
                throw std::runtime_error("invalid staging data or RowPitch");
            }
            uint64_t contentHash = hashBasis;
            const auto* pixels = static_cast<const BYTE*>(mapping.data.pData);
            for (UINT y = 0; y < desc.Height; ++y) {
                // Hash only visible bytes; padding up to RowPitch is intentionally excluded.
                hashBytes(contentHash, pixels + static_cast<size_t>(y) * mapping.data.RowPitch, rowBytes);
            }
            if (stats.readable && contentHash != stats.lastHash) ++stats.changed;
            stats.lastHash = contentHash;
            // Aggregate the per-frame hashes in a defined little-endian byte order.
            for (unsigned shift = 0; shift < 64; shift += 8) {
                const BYTE byte = static_cast<BYTE>(contentHash >> shift);
                hashBytes(stats.streamHash, &byte, 1);
            }
            ++stats.readable;
            stats.bytes += static_cast<uint64_t>(rowBytes) * desc.Height;
            stats.accumulated += info.AccumulatedFrames;
            if (stats.readable == 1) {
                std::cout << "FIRST_READABLE_FRAME width=" << desc.Width << " height=" << desc.Height
                          << " row_pitch=" << mapping.data.RowPitch << " visible_row_bytes=" << rowBytes
                          << " accumulated_frames=" << info.AccumulatedFrames
                          << " protected_content_masked=" << info.ProtectedContentMaskedOut
                          << " fnv1a64=" << hex(contentHash, 16) << '\n';
            }
        } // Unmap before releasing the duplication frame.
        frame.release();
    }
    stats.complete = true;
    if (!stats.readable) {
        std::cerr << "NO_DATA: no CPU-readable DXGI frames within the sampling window.\n";
        return 3;
    }
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    // The calling/main thread IS the capture thread: initialize, use and release
    // all COM objects here. No apartment-bound interface crosses a thread.
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    if (argc == 2 && std::strcmp(argv[1], "--help") == 0) { usage(); return 0; }
    try {
        const Options options = parse(argc, argv);
        std::cout << "PROBE mode=" << options.mode << " seconds=" << options.seconds
                  << " list_only=" << options.listOnly << " capture_thread_id=" << GetCurrentThreadId()
                  << " com=MTA hash=FNV1a64 raw_content_written=false\n";
        int result = 1;
        {
            ComApartment apartment;
            result = options.mode == "wasapi" ? runWasapi(options) : runDxgi(options);
        }
        std::cout << "RESULT exit_code=" << result << '\n';
        return result;
    } catch (const std::invalid_argument& error) {
        std::cerr << "CLI_ERROR " << error.what() << '\n';
        usage();
        std::cerr << "RESULT exit_code=2\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "ERROR " << error.what() << "\nRESULT exit_code=1\n";
        return 1;
    } catch (...) {
        std::cerr << "ERROR unknown exception\nRESULT exit_code=1\n";
        return 1;
    }
}

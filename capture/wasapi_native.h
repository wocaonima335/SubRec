#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <functiondiscoverykeys_devpkey.h>
#include <atomic>
#include <stdexcept>
#include <string>

namespace capture::wasapi::native {
struct Error { HRESULT code; const char *operation; };
inline void check(HRESULT hr, const char *op) { if (FAILED(hr)) throw Error{hr, op}; }
template<class T> struct ComPtr {
    T *p = nullptr;
    ~ComPtr() { reset(); }
    ComPtr() = default;
    ComPtr(const ComPtr &) = delete;
    void reset() { if (p) p->Release(); p = nullptr; }
    T **put() { reset(); return &p; }
    T *operator->() const { return p; }
};
struct ComScope {
    ComScope() { check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx"); }
    ~ComScope() { CoUninitialize(); }
};
template<class T> struct TaskMemory {
    T *p = nullptr;
    ~TaskMemory() { CoTaskMemFree(p); }
};
struct Event {
    HANDLE handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Event() { if (!handle) throw Error{HRESULT_FROM_WIN32(GetLastError()), "CreateEvent"}; }
    ~Event() { CloseHandle(handle); }
};
inline int64_t qpc100ns() {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
    return now.QuadPart / frequency.QuadPart * 10000000
        + now.QuadPart % frequency.QuadPart * 10000000 / frequency.QuadPart;
}
struct Notification final : IMMNotificationClient {
    std::atomic<ULONG> refs{1};
    std::atomic<bool> changed{false};
    ERole role;
    explicit Notification(ERole r) : role(r) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient *>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole r, LPCWSTR) override {
        if (flow == eRender && r == role) changed = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
};
struct Stream {
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    TaskMemory<WAVEFORMATEX> format;
    bool started = false;
    ~Stream() { if (started) client->Stop(); }
};
struct Packet {
    IAudioCaptureClient *client;
    UINT32 frames;
    ~Packet() { client->ReleaseBuffer(frames); }
};
} // namespace

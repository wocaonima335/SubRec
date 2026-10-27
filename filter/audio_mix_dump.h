#pragma once
#include <memory>
#include <cstdint>
struct AVFrame;

// Optional diagnostic writer. Owns a bounded I/O worker; all failures disable
// only the dump. Files contain the exact aligned/gained inputs to amix.
class AudioMixDump {
public:
    static std::unique_ptr<AudioMixDump> fromEnvironment(int64_t startUs) noexcept;
    ~AudioMixDump();
    void submit(const AVFrame &system, const AVFrame &microphone) noexcept;
private:
    struct Impl;
    explicit AudioMixDump(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> d;
};

#include "subtitle/tap/audio_tap.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <vector>

#if SUBREC_TAP_HAS_FFMPEG
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}
#endif

namespace subtitle {

struct AudioTap::Impl {
    struct Entry { int64_t pts; uint64_t start; int count; bool splice; };
    // Mirrored sample storage makes even a wrapped block contiguous until release.
    // Descriptors are independently bounded: at most 4096 outstanding frames.
    explicit Impl(size_t n) : capacity(n), pcm(2 * n), entries(std::min(n, size_t{4096})) {}
    const size_t capacity;
    std::vector<float> pcm;
    std::vector<Entry> entries;
    alignas(64) std::atomic<uint64_t> head{0};
    uint64_t written = 0; // producer only
    alignas(64) std::atomic<uint64_t> tail{0}, released{0};
    bool held = false; // consumer only
    std::atomic<bool> finished{false}, aborted{false};
    // Separate acknowledgements prevent a delayed consumer clear from erasing a
    // newer mark. Only the producer writes carried, only the consumer writes consumed.
    std::atomic<uint64_t> requested{0}, carried{0}, consumed{0};
    mutable uint64_t observedRequest = 0; // consumer only
    std::atomic<uint64_t> pushed{0}, droppedBlocks{0}, droppedSamples{0};
    std::atomic<uint64_t> formatRejects{0}, maxPushNs{0};
};

static_assert(std::atomic<uint64_t>::is_always_lock_free, "AudioTap requires lock-free counters");
static_assert(std::atomic<bool>::is_always_lock_free, "AudioTap requires lock-free flags");

AudioTap::AudioTap(int capacityMs)
{
    if (capacityMs <= 0 || uint64_t(capacityMs) * 48 > std::numeric_limits<size_t>::max() / (2 * sizeof(float)))
        throw std::invalid_argument("AudioTap capacityMs");
    m_impl = new Impl(size_t(capacityMs) * 48);
}
AudioTap::~AudioTap() { delete m_impl; }

void AudioTap::push(const AVFrame &frame) noexcept
{
    auto &s = *m_impl;
    if (s.finished.load(std::memory_order_acquire) || s.aborted.load(std::memory_order_acquire)) return;
    const auto begin = std::chrono::steady_clock::now();
    // Single producer: no CAS/retry loop is needed to maintain the maximum.
    struct Timer {
        Impl &s;
        std::chrono::steady_clock::time_point begin;
        ~Timer() {
            const auto ns = std::max<int64_t>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - begin).count());
            if (uint64_t(ns) > s.maxPushNs.load(std::memory_order_relaxed))
                s.maxPushNs.store(uint64_t(ns), std::memory_order_relaxed);
        }
    } timer{s, begin};
#if SUBREC_TAP_HAS_FFMPEG
    const int channels = frame.ch_layout.nb_channels;
    const bool planar = frame.format == AV_SAMPLE_FMT_FLTP;
    auto data = frame.extended_data ? frame.extended_data : frame.data;
    if (frame.sample_rate != 48000 || (channels != 1 && channels != 2) ||
        (!planar && frame.format != AV_SAMPLE_FMT_FLT) || frame.nb_samples <= 0 ||
        frame.pts == AV_NOPTS_VALUE || !data[0] || (planar && channels == 2 && !data[1])) {
        s.formatRejects.store(s.formatRejects.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        return;
    }
    const auto n = uint64_t(frame.nb_samples);
    const auto head = s.head.load(std::memory_order_relaxed);
    if (n > s.capacity || head - s.tail.load(std::memory_order_acquire) == s.entries.size() ||
        s.written - s.released.load(std::memory_order_acquire) > s.capacity - n) {
        s.droppedBlocks.store(s.droppedBlocks.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        s.droppedSamples.store(s.droppedSamples.load(std::memory_order_relaxed) + n, std::memory_order_relaxed);
        return;
    }
    const auto *left = reinterpret_cast<const float *>(data[0]);
    const auto *right = planar && channels == 2 ? reinterpret_cast<const float *>(data[1]) : nullptr;
    size_t pos = size_t(s.written % s.capacity);
    for (int i = 0; i < frame.nb_samples; ++i) {
        const float value = channels == 1 ? left[i] : planar ?
            left[i] * 0.5f + right[i] * 0.5f : left[2 * i] * 0.5f + left[2 * i + 1] * 0.5f;
        s.pcm[pos] = s.pcm[pos + s.capacity] = value;
        if (++pos == s.capacity) pos = 0;
    }
    const auto request = s.requested.load(std::memory_order_acquire);
    const bool splice = request > std::max(s.carried.load(std::memory_order_relaxed),
                                           s.consumed.load(std::memory_order_acquire));
    s.entries[head % s.entries.size()] = {frame.pts, s.written, frame.nb_samples, splice};
    s.written += n;
    s.carried.store(request, std::memory_order_release);
    s.head.store(head + 1, std::memory_order_release);
    s.pushed.store(s.pushed.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
#else
    (void)frame;
    s.formatRejects.store(s.formatRejects.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
#endif
}

void AudioTap::markSplice() noexcept
{
    auto &request = m_impl->requested;
    request.store(request.load(std::memory_order_relaxed) + 1, std::memory_order_release);
}
void AudioTap::finish() noexcept { m_impl->finished.store(true, std::memory_order_release); }
void AudioTap::abort() noexcept { m_impl->aborted.store(true, std::memory_order_release); }

AudioTap::Pop AudioTap::pop(Block &out) noexcept
{
    auto &s = *m_impl;
    out = {0, 0, false, nullptr};
    if (s.aborted.load(std::memory_order_acquire)) return Pop::Aborted;
    const auto tail = s.tail.load(std::memory_order_relaxed);
    // Read finish before head: seeing finish must also expose every preceding push.
    const bool finished = s.finished.load(std::memory_order_acquire);
    if (tail == s.head.load(std::memory_order_acquire)) return finished ? Pop::Eof : Pop::Empty;
    const auto &entry = s.entries[tail % s.entries.size()];
    out = {entry.pts, entry.count, entry.splice, s.pcm.data() + entry.start % s.capacity};
    s.held = true;
    return Pop::Data;
}
void AudioTap::release() noexcept
{
    auto &s = *m_impl;
    if (!s.held) return;
    const auto tail = s.tail.load(std::memory_order_relaxed);
    const auto &entry = s.entries[tail % s.entries.size()];
    s.released.store(entry.start + uint64_t(entry.count), std::memory_order_release);
    s.tail.store(tail + 1, std::memory_order_release);
    s.held = false;
}
bool AudioTap::spliceRequested() const noexcept
{
    auto &s = *m_impl;
    s.observedRequest = s.requested.load(std::memory_order_acquire);
    return s.observedRequest > std::max(s.carried.load(std::memory_order_acquire),
                                       s.consumed.load(std::memory_order_relaxed));
}
void AudioTap::consumeSpliceRequest() noexcept
{
    // Normally paired with spliceRequested; a direct call also consumes pending marks.
    auto &s = *m_impl;
    const auto request = s.observedRequest ? s.observedRequest : s.requested.load(std::memory_order_acquire);
    s.consumed.store(std::max(request, s.consumed.load(std::memory_order_relaxed)), std::memory_order_release);
    s.observedRequest = 0;
}
AudioTap::Stats AudioTap::stats() const noexcept
{
    const auto &s = *m_impl;
    return {s.pushed.load(std::memory_order_relaxed), s.droppedBlocks.load(std::memory_order_relaxed),
            s.droppedSamples.load(std::memory_order_relaxed), s.formatRejects.load(std::memory_order_relaxed),
            s.maxPushNs.load(std::memory_order_relaxed), s.requested.load(std::memory_order_relaxed)};
}
} // namespace subtitle

#include "subtitle/audio/ingest_worker.h"
#include "subtitle/audio/mono_resampler_16k.h"
#include "subtitle/audio/pcm_spool.h"
#include "subtitle/tap/audio_tap.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace subtitle {
namespace {
struct WorkerState {
    std::shared_ptr<AudioTap> tap;
    std::shared_ptr<PcmSpool> spool;
    IClock &clock;
    std::thread thread;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> blocks{0}, gaps{0}, gapSamples{0}, splices{0};
    mutable std::mutex mutex;
    std::condition_variable doneCondition;
    bool started = false, done = false;
    Status error;

    WorkerState(std::shared_ptr<AudioTap> t, std::shared_ptr<PcmSpool> s, IClock &c)
        : tap(std::move(t)), spool(std::move(s)), clock(c) {}
    void fail(Status status) {
        if (status.ok()) return;
        std::lock_guard<std::mutex> lock(mutex);
        if (error.ok()) error = std::move(status);
    }
    void run() noexcept {
        try { ingest(); }
        catch (const std::bad_alloc &) { fail(Status::error(ErrorCode::OutOfMemory)); drain(); }
        catch (...) { fail(Status::error(ErrorCode::InternalError)); drain(); }
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
        }
        doneCondition.notify_all();
    }
    void drain() noexcept {
        // Even allocation/unexpected failures must not leave the producer backed up.
        while (!stop.load(std::memory_order_acquire)) {
            AudioTap::Block block{};
            const auto result = tap->pop(block);
            if (result == AudioTap::Pop::Data) { tap->release(); ++blocks; }
            else if (result == AudioTap::Pop::Empty) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            else break;
        }
    }
    void ingest() {
        MonoResampler16k resampler;
        std::vector<float> output;
        output.reserve(4096);
        const std::array<float, 4096> silence{};
        int64_t expected48k = 0;
        int64_t lastPublish = clock.nowMs();
        bool writable = true;
        auto append = [&] {
            if (writable && !output.empty()) {
                auto status = spool->append(output.data(), output.size());
                if (!status.ok()) { fail(std::move(status)); writable = false; }
            }
            output.clear();
        };
        while (!stop.load(std::memory_order_acquire)) {
            AudioTap::Block block{};
            const auto result = tap->pop(block);
            if (result == AudioTap::Pop::Aborted) return;
            if (result == AudioTap::Pop::Eof) {
                if (writable) {
                    resampler.flush(output);
                    append();
                    spool->publish(spool->writtenSamples());
                    if (writable) fail(spool->finalize());
                }
                return;
            }
            if (result == AudioTap::Pop::Data) {
                // Release even when vector allocation or a clock implementation throws.
                struct Release { AudioTap &tap; ~Release() { tap.release(); } } release{*tap};
                ++blocks;
                if (block.splice) {
                    spool->markSplice16k(block.pts48k / 3);
                    ++splices;
                }
                if (block.pts48k > expected48k) {
                    auto gap = uint64_t(block.pts48k) - uint64_t(expected48k);
                    ++gaps;
                    gapSamples.fetch_add(gap, std::memory_order_relaxed);
                    while (gap && writable && !stop.load(std::memory_order_acquire)) {
                        const size_t n = size_t(std::min<uint64_t>(gap, silence.size()));
                        resampler.process(silence.data(), n, output);
                        append();
                        gap -= n;
                    }
                    expected48k = block.pts48k;
                }
                // Ignore duplicate/overlapping samples without moving the timeline back.
                const uint64_t overlap = block.pts48k < expected48k ?
                    uint64_t(expected48k) - uint64_t(block.pts48k) : 0;
                const size_t skip = size_t(std::min<uint64_t>(overlap, uint64_t(block.samples)));
                if (writable && !stop.load(std::memory_order_acquire)) {
                    resampler.process(block.mono + skip, size_t(block.samples) - skip, output);
                    append();
                }
                const int64_t remaining = block.samples - int64_t(skip);
                if (remaining > std::numeric_limits<int64_t>::max() - expected48k) {
                    fail(Status::error(ErrorCode::InvalidArgument, "audio pts overflow"));
                    writable = false;
                } else expected48k += remaining;
            }
            const auto now = clock.nowMs();
            if (writable && now - lastPublish >= 100) {
                spool->publish(spool->writtenSamples());
                lastPublish = now;
            }
            if (result == AudioTap::Pop::Empty) {
                if (tap->spliceRequested()) {
                    spool->markSplice16k(spool->committedSamples());
                    tap->consumeSpliceRequest();
                    ++splices;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
};

// Keep the frozen IngestWorker layout intact. The existing shared_ptr's control
// block owns the opaque state through a typed deleter; get() still returns the
// original AudioTap. No global registry or lifetime coupling between jobs.
struct WorkerOwner {
    std::shared_ptr<WorkerState> state;
    void operator()(AudioTap *) const noexcept {}
};
WorkerState &state(const std::shared_ptr<AudioTap> &tap)
{
    return *std::get_deleter<WorkerOwner>(tap)->state;
}
} // namespace

IngestWorker::IngestWorker(std::shared_ptr<AudioTap> tap, std::shared_ptr<PcmSpool> spool, IClock &clock)
    : m_tap(tap.get(), WorkerOwner{std::make_shared<WorkerState>(tap, spool, clock)}),
      m_spool(std::move(spool)), m_clock(clock) {}
IngestWorker::~IngestWorker()
{
    requestStop();
    auto &s = state(m_tap);
    if (s.thread.joinable()) s.thread.join();
}
Status IngestWorker::start()
{
    auto &s = state(m_tap);
    if (s.started) return Status::error(ErrorCode::NotReady, "ingest already started");
    if (!s.tap || !s.spool) return Status::error(ErrorCode::InvalidArgument);
    try {
        s.thread = std::thread([&s] { s.run(); });
        s.started = true;
        return {};
    } catch (const std::bad_alloc &) { return Status::error(ErrorCode::OutOfMemory); }
    catch (const std::exception &e) { return Status::error(ErrorCode::InternalError, e.what()); }
}
void IngestWorker::requestStop() { state(m_tap).stop.store(true, std::memory_order_release); }
bool IngestWorker::join(std::chrono::milliseconds timeout)
{
    auto &s = state(m_tap);
    if (!s.started || !s.thread.joinable()) return true;
    std::unique_lock<std::mutex> lock(s.mutex);
    if (!s.doneCondition.wait_for(lock, timeout, [&s] { return s.done; })) return false;
    lock.unlock();
    s.thread.join();
    return true;
}
IngestWorker::Stats IngestWorker::stats() const
{
    auto &s = state(m_tap);
    return {s.blocks.load(), s.gaps.load(), s.gapSamples.load(), s.splices.load()};
}
Status IngestWorker::lastError() const
{
    auto &s = state(m_tap);
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.error;
}
} // namespace subtitle

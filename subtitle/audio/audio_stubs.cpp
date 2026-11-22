// M0 桩：subtitle_audio（MonoResampler16k、PcmSpool、IngestWorker）。
// 真实实现归工作包 A；实现时请拆成 mono_resampler_16k.cpp / pcm_spool.cpp / ingest_worker.cpp，并删除本文件。
// 桩行为：不做重采样、不写文件、不起线程；需要返回 Status 的函数一律返回 NotSupported。

#include "subtitle/audio/ingest_worker.h"
#include "subtitle/audio/mono_resampler_16k.h"
#include "subtitle/audio/pcm_spool.h"

#include "subtitle/tap/audio_tap.h"

#include <utility>

namespace subtitle {

namespace {
Status m0Stub(const char *what)
{
    return Status::error(ErrorCode::NotSupported, std::string("M0 stub: ") + what);
}
} // namespace

// ---- MonoResampler16k ----
MonoResampler16k::MonoResampler16k() = default;
void MonoResampler16k::process(const float *, size_t, std::vector<float> &) {}
void MonoResampler16k::flush(std::vector<float> &) {}
void MonoResampler16k::reset() { m_history.clear(); }

// ---- PcmSpool ----
PcmSpool::PcmSpool() = default;
PcmSpool::~PcmSpool() = default;
Status PcmSpool::create(const std::filesystem::path &wav)
{
    m_path = wav;
    return m0Stub("PcmSpool::create");
}
Status PcmSpool::append(const float *, size_t) { return m0Stub("PcmSpool::append"); }
void PcmSpool::publish(int64_t committed16k) noexcept { m_committed.store(committed16k); }
void PcmSpool::markSplice16k(int64_t at16k) noexcept { m_splice.store(at16k); }
Status PcmSpool::finalize() { return m0Stub("PcmSpool::finalize"); }
int64_t PcmSpool::writtenSamples() const noexcept { return m_written; }
std::filesystem::path PcmSpool::path() const { return m_path; }
int64_t PcmSpool::committedSamples() const noexcept { return m_committed.load(); }
int64_t PcmSpool::takeSplice() noexcept { return m_splice.exchange(-1); }

// ---- IngestWorker ----
IngestWorker::IngestWorker(std::shared_ptr<AudioTap> tap, std::shared_ptr<PcmSpool> spool, IClock &clock)
    : m_tap(std::move(tap)), m_spool(std::move(spool)), m_clock(clock)
{
}
IngestWorker::~IngestWorker() = default;
Status IngestWorker::start() { return m0Stub("IngestWorker::start"); }
void IngestWorker::requestStop() {}
bool IngestWorker::join(std::chrono::milliseconds) { return true; }
IngestWorker::Stats IngestWorker::stats() const { return {}; }
Status IngestWorker::lastError() const { return {}; }

} // namespace subtitle

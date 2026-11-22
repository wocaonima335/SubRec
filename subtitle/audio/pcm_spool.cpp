#include "subtitle/audio/pcm_spool.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#ifdef _WIN32
#include <share.h>
#endif

namespace subtitle {
namespace {
struct File {
    std::FILE *stream = nullptr;
    Status error;
    bool finalized = false;
    ~File() { if (stream) std::fclose(stream); }
};
Status ioError(ErrorCode fallback)
{
    const int error = errno;
    return Status::error(error == ENOSPC
#ifdef EDQUOT
                         || error == EDQUOT
#endif
                         ? ErrorCode::SpoolDiskFull : fallback, std::strerror(error));
}
void le16(unsigned char *out, uint16_t value)
{
    out[0] = static_cast<unsigned char>(value);
    out[1] = static_cast<unsigned char>(value >> 8);
}
void le32(unsigned char *out, uint32_t value)
{
    le16(out, uint16_t(value));
    le16(out + 2, uint16_t(value >> 16));
}
std::array<unsigned char, 44> header(uint32_t bytes)
{
    std::array<unsigned char, 44> h{};
    std::memcpy(h.data(), "RIFF", 4);
    le32(h.data() + 4, bytes + 36);
    std::memcpy(h.data() + 8, "WAVEfmt ", 8);
    le32(h.data() + 16, 16);
    le16(h.data() + 20, 1); // PCM
    le16(h.data() + 22, 1);
    le32(h.data() + 24, 16000);
    le32(h.data() + 28, 32000);
    le16(h.data() + 32, 2);
    le16(h.data() + 34, 16);
    std::memcpy(h.data() + 36, "data", 4);
    le32(h.data() + 40, bytes);
    return h;
}
} // namespace

PcmSpool::PcmSpool() = default;
PcmSpool::~PcmSpool() { delete static_cast<File *>(m_file); }
Status PcmSpool::create(const std::filesystem::path &wav)
{
    try {
        auto file = std::make_unique<File>();
#ifdef _WIN32
        file->stream = _wfsopen(wav.c_str(), L"wb", _SH_DENYNO);
#else
        file->stream = std::fopen(wav.c_str(), "wb");
#endif
        if (!file->stream) return ioError(ErrorCode::SpoolOpenFailed);
        // No userspace buffering: append's successful return means bytes have
        // reached the OS cache and are visible to independently opened readers.
        if (std::setvbuf(file->stream, nullptr, _IONBF, 0) != 0)
            return ioError(ErrorCode::SpoolOpenFailed);
        const auto h = header(0);
        if (std::fwrite(h.data(), 1, h.size(), file->stream) != h.size())
            return ioError(ErrorCode::SpoolWriteFailed);
        m_path = wav;
        delete static_cast<File *>(m_file);
        m_file = file.release();
        m_written = 0;
        m_committed.store(0, std::memory_order_release);
        m_splice.store(-1, std::memory_order_release);
        return {};
    } catch (const std::bad_alloc &) {
        return Status::error(ErrorCode::OutOfMemory);
    } catch (const std::exception &e) {
        return Status::error(ErrorCode::SpoolOpenFailed, e.what());
    }
}
Status PcmSpool::append(const float *pcm, size_t n) try
{
    auto *file = static_cast<File *>(m_file);
    if (!file || file->finalized) return Status::error(ErrorCode::NotReady);
    if (!file->error.ok()) return file->error;
    if (n && !pcm) return Status::error(ErrorCode::InvalidArgument);
    // Ordinary RIFF/WAV has 32-bit sizes. Refuse overflow instead of wrapping a
    // length and producing a corrupt file (about 37 hours at 16 kHz mono s16).
    constexpr uint64_t maxSamples = (uint64_t(UINT32_MAX) - 36) / 2;
    if (uint64_t(n) > maxSamples - uint64_t(m_written))
        return file->error = Status::error(ErrorCode::SpoolWriteFailed, "RIFF size limit exceeded");
    std::array<unsigned char, 8192> bytes{};
    while (n) {
        const size_t count = std::min(n, bytes.size() / 2);
        for (size_t i = 0; i < count; ++i) {
            const float value = std::isnan(pcm[i]) ? 0.f : std::clamp(pcm[i], -1.f, 1.f);
            const auto sample = std::clamp(std::lround(double(value) * 32768), -32768L, 32767L);
            le16(bytes.data() + 2 * i, static_cast<uint16_t>(sample));
        }
        const size_t written = std::fwrite(bytes.data(), 1, count * 2, file->stream);
        m_written += int64_t(written / 2);
        if (written != count * 2) return file->error = ioError(ErrorCode::SpoolWriteFailed);
        pcm += count;
        n -= count;
    }
    return {};
}
catch (const std::bad_alloc &) { return Status::error(ErrorCode::OutOfMemory); }
catch (...) { return Status::error(ErrorCode::SpoolWriteFailed); }
void PcmSpool::publish(int64_t committed) noexcept
{
    const int64_t previous = m_committed.load(std::memory_order_relaxed);
    m_committed.store(std::max(previous, std::min(committed, m_written)), std::memory_order_release);
}
void PcmSpool::markSplice16k(int64_t at) noexcept { m_splice.store(at, std::memory_order_release); }
Status PcmSpool::finalize() try
{
    auto *file = static_cast<File *>(m_file);
    if (!file) return Status::error(ErrorCode::NotReady);
    if (!file->error.ok()) return file->error;
    if (file->finalized) return {};
    const auto h = header(uint32_t(m_written * 2));
    if (std::fseek(file->stream, 0, SEEK_SET) != 0 ||
        std::fwrite(h.data(), 1, h.size(), file->stream) != h.size())
        return file->error = ioError(ErrorCode::SpoolFinalizeFailed);
    const int result = std::fclose(file->stream);
    file->stream = nullptr;
    file->finalized = true;
    if (result != 0) return file->error = ioError(ErrorCode::SpoolFinalizeFailed);
    return {};
}
catch (const std::bad_alloc &) { return Status::error(ErrorCode::OutOfMemory); }
catch (...) { return Status::error(ErrorCode::SpoolFinalizeFailed); }
int64_t PcmSpool::writtenSamples() const noexcept { return m_written; }
std::filesystem::path PcmSpool::path() const { return m_path; }
int64_t PcmSpool::committedSamples() const noexcept { return m_committed.load(std::memory_order_acquire); }
int64_t PcmSpool::takeSplice() noexcept { return m_splice.exchange(-1, std::memory_order_acq_rel); }
} // namespace subtitle

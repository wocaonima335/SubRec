// spool 尾随读取（工作包 C，design-live-v2.md §7.1、§8.1）。
//
// spool 是 16 kHz 单声道 s16le 的追加式 WAV（PcmSpool，工作包 A）。录制中 RIFF 头里的长度是陈旧的
// （finalize 时才回填），因此读取位置一律按 committed16k × bytesPerSample 从 data 块偏移直接换算，
// 不信任头部长度字段。兼容 float32（audioFormat=3）便于测试。只读共享打开，不给写者添堵。

#include "host_runtime.h"

#include "subtitle/subtitle_error.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#include <share.h>
#include <windows.h>
#else
#include <sys/types.h>
#endif

namespace asrhost {

namespace {

uint32_t readU32(const unsigned char *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t readU16(const unsigned char *p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

bool seek64(std::FILE *f, int64_t offset)
{
#if defined(_WIN32)
    return _fseeki64(f, offset, SEEK_SET) == 0;
#elif defined(__unix__) || defined(__APPLE__)
    return fseeko(f, static_cast<off_t>(offset), SEEK_SET) == 0;
#else
    return fseek(f, static_cast<long>(offset), SEEK_SET) == 0;
#endif
}

std::FILE *openReadShared(const std::string &pathUtf8)
{
#if defined(_WIN32)
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, pathUtf8.c_str(), -1, nullptr, 0);
    if (wlen <= 0)
        return nullptr;
    std::wstring wide(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, pathUtf8.c_str(), -1, wide.data(), wlen);
    return _wfsopen(wide.c_str(), L"rb", _SH_DENYNO);
#else
    return std::fopen(pathUtf8.c_str(), "rb");
#endif
}

} // namespace

subtitle::Status SpoolTailReader::open(const std::string &spoolPathUtf8)
{
    close();
    std::FILE *f = openReadShared(spoolPathUtf8);
    if (!f)
        return subtitle::Status::error(subtitle::ErrorCode::SpoolOpenFailed,
                                       "cannot open spool: " + spoolPathUtf8);

    unsigned char hdr[12];
    if (std::fread(hdr, 1, sizeof hdr, f) != sizeof hdr || std::memcmp(hdr, "RIFF", 4) != 0
        || std::memcmp(hdr + 8, "WAVE", 4) != 0) {
        std::fclose(f);
        return subtitle::Status::error(subtitle::ErrorCode::SpoolCorrupt, "spool: not a RIFF/WAVE file");
    }

    bool haveFmt = false;
    uint16_t formatTag = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    uint64_t dataOffset = 0;
    bool haveData = false;
    for (;;) {
        unsigned char ch[8];
        if (std::fread(ch, 1, sizeof ch, f) != sizeof ch)
            break;
        const uint32_t size = readU32(ch + 4);
        if (std::memcmp(ch, "fmt ", 4) == 0) {
            unsigned char fmt[16];
            const uint32_t take = std::min<uint32_t>(size, sizeof fmt);
            if (std::fread(fmt, 1, take, f) != take)
                break;
            if (size > take
                && !seek64(f, static_cast<int64_t>(ftell(f)) + static_cast<int64_t>(size - take)))
                break;
            formatTag = readU16(fmt);
            channels = readU16(fmt + 2);
            rate = readU32(fmt + 4);
            bits = readU16(fmt + 14);
            haveFmt = true;
        } else if (std::memcmp(ch, "data", 4) == 0) {
            dataOffset = static_cast<uint64_t>(ftell(f));
            haveData = true;
            break;   // data 是最后一块且持续增长，其后没有（也不会有）别的块
        } else {
            if (!seek64(f, static_cast<int64_t>(ftell(f)) + static_cast<int64_t>(size) + (size & 1)))
                break;
        }
    }

    if (!haveFmt || !haveData) {
        std::fclose(f);
        return subtitle::Status::error(subtitle::ErrorCode::SpoolCorrupt, "spool: missing fmt/data chunk");
    }
    if (rate != static_cast<uint32_t>(subtitle::kAsrSampleRate) || channels != 1) {
        std::fclose(f);
        return subtitle::Status::error(subtitle::ErrorCode::SpoolCorrupt, "spool: must be mono 16 kHz");
    }
    if (formatTag == 1 && bits == 16) {
        m_formatTag = 1;
        m_bytesPerSample = 2;
    } else if (formatTag == 3 && bits == 32) {
        m_formatTag = 3;
        m_bytesPerSample = 4;
    } else {
        std::fclose(f);
        return subtitle::Status::error(subtitle::ErrorCode::SpoolCorrupt, "spool: unsupported sample format");
    }

    m_file = f;
    m_dataOffset = dataOffset;
    m_sampleRate = static_cast<int>(rate);
    m_channels = static_cast<int>(channels);
    m_committed.store(0);
    return {};
}

void SpoolTailReader::setCommitted(int64_t committed16k)
{
    int64_t prev = m_committed.load(std::memory_order_relaxed);
    while (committed16k > prev
           && !m_committed.compare_exchange_weak(prev, committed16k, std::memory_order_relaxed)) {
    }
}

int64_t SpoolTailReader::committed() const
{
    return m_committed.load(std::memory_order_relaxed);
}

subtitle::Status SpoolTailReader::read(int64_t from16k, int64_t to16k, std::vector<float> &out)
{
    out.clear();
    if (!m_file)
        return subtitle::Status::error(subtitle::ErrorCode::SpoolReadFailed, "spool not open");
    if (to16k < from16k || from16k < 0)
        return subtitle::Status::error(subtitle::ErrorCode::InvalidArgument, "bad read range");
    if (to16k > m_committed.load(std::memory_order_relaxed))
        return subtitle::Status::error(subtitle::ErrorCode::SpoolRangeNotCommitted, "read beyond committed16k");

    const int64_t n = to16k - from16k;
    if (n == 0)
        return {};
    out.resize(static_cast<size_t>(n));

    if (!seek64(m_file, static_cast<int64_t>(m_dataOffset) + from16k * m_bytesPerSample))
        return subtitle::Status::error(subtitle::ErrorCode::SpoolReadFailed, "seek failed");

    if (m_formatTag == 1) {
        std::vector<int16_t> raw(static_cast<size_t>(n));
        if (std::fread(raw.data(), sizeof(int16_t), raw.size(), m_file) != raw.size())
            return subtitle::Status::error(subtitle::ErrorCode::SpoolReadFailed, "short read");
        for (size_t i = 0; i < raw.size(); ++i)
            out[i] = static_cast<float>(raw[i]) / 32768.f;
    } else {
        std::vector<float> raw(static_cast<size_t>(n));
        if (std::fread(raw.data(), sizeof(float), raw.size(), m_file) != raw.size())
            return subtitle::Status::error(subtitle::ErrorCode::SpoolReadFailed, "short read");
        out = std::move(raw);
    }
    return {};
}

void SpoolTailReader::close()
{
    if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
    }
}

} // namespace asrhost

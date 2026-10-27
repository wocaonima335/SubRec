#pragma once

// Test-only RIFF reader: no dependency on optional FFmpeg WAV demuxers/codecs.
// Accept 48 kHz mono/stereo PCM 8/16/24/32-bit and IEEE float 32-bit WAV.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace audio_test {
inline std::array<std::vector<float>, 2> readWav(const std::filesystem::path &path) {
    auto expect = [](bool ok, const char *message) {
        if (!ok) throw std::runtime_error(message);
    };
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    expect(file.is_open(), "cannot open configured WAV file");
    const auto length = file.tellg();
    expect(length >= 12 && length <= 128 * 1024 * 1024, "WAV must be 12 bytes..128 MiB");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    file.seekg(0);
    expect(bool(file.read(reinterpret_cast<char *>(bytes.data()), bytes.size())), "cannot read WAV");
    const auto u16 = [&](size_t p) { return unsigned(bytes[p]) | (unsigned(bytes[p+1]) << 8); };
    const auto u32 = [&](size_t p) { return uint32_t(u16(p)) | (uint32_t(u16(p+2)) << 16); };
    const auto tag = [&](size_t p, const char *name) { return std::memcmp(bytes.data()+p, name, 4) == 0; };
    expect(tag(0, "RIFF") && tag(8, "WAVE"), "expected little-endian RIFF WAVE");
    const uint64_t end = uint64_t(u32(4)) + 8;
    expect(end >= 12 && end <= bytes.size(), "truncated RIFF");
    size_t fmt = 0, fmtSize = 0, data = 0, dataSize = 0;
    for (size_t p = 12; p < end;) {
        expect(end - p >= 8, "truncated WAV chunk header");
        const auto size = u32(p+4);
        expect(uint64_t(size) <= end - p - 8, "truncated WAV chunk");
        if (tag(p, "fmt ")) {
            expect(!fmt, "duplicate WAV fmt chunk"); fmt = p+8; fmtSize = size;
        } else if (tag(p, "data")) {
            expect(!data, "duplicate WAV data chunk"); data = p+8; dataSize = size;
        }
        p += 8 + uint64_t(size) + (size & 1);
        expect(p <= end, "missing WAV chunk padding");
    }
    expect(fmt && fmtSize >= 16 && data && dataSize, "WAV requires fmt and non-empty data");
    unsigned format = u16(fmt), channels = u16(fmt+2), bits = u16(fmt+14);
    expect(u32(fmt+4) == 48000 && (channels == 1 || channels == 2), "WAV must be 48 kHz mono/stereo");
    if (format == 0xfffe) {
        expect(fmtSize >= 40 && u16(fmt+16) >= 22, "invalid extensible WAV format");
        const uint8_t guidTail[] = {0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
        expect(std::memcmp(bytes.data()+fmt+26, guidTail, sizeof(guidTail)) == 0,
               "unsupported extensible WAV GUID");
        expect(u16(fmt+18) > 0 && u16(fmt+18) <= bits, "invalid WAV valid bits");
        const auto mask = u32(fmt+20);
        expect(mask == 0 || (channels == 1 ? mask == 4 : mask == 3), "unsupported WAV channel mask");
        format = u16(fmt+24);
    }
    expect((format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) ||
           (format == 3 && bits == 32), "unsupported WAV: use PCM 8/16/24/32 or float32");
    const unsigned sampleBytes = bits/8, stride = channels*sampleBytes;
    expect(u16(fmt+12) == stride && u32(fmt+8) == 48000*stride && dataSize % stride == 0,
           "invalid WAV sample layout");
    std::array<std::vector<float>, 2> result;
    for (auto &channel : result) channel.resize(dataSize/stride);
    for (size_t n = 0; n < result[0].size(); ++n) for (unsigned ch = 0; ch < channels; ++ch) {
        const size_t p = data + n*stride + ch*sampleBytes;
        float value;
        if (format == 3) {
            const uint32_t raw = u32(p); std::memcpy(&value, &raw, sizeof(value));
        } else if (bits == 8) {
            value = (int(bytes[p])-128)/128.f;
        } else {
            uint32_t raw = 0;
            for (unsigned b = 0; b < sampleBytes; ++b) raw |= uint32_t(bytes[p+b]) << (8*b);
            const int64_t signedValue = (raw & (uint32_t(1) << (bits-1)))
                ? int64_t(raw) - (int64_t(1) << bits) : int64_t(raw);
            value = float(double(signedValue) / double(int64_t(1) << (bits-1)));
        }
        expect(std::isfinite(value), "non-finite WAV sample");
        result[ch][n] = value;
        if (channels == 1) result[1][n] = value;
    }
    return result;
}
} // namespace audio_test

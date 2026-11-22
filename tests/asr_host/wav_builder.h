#pragma once
// tests/asr_host：合成 16 kHz 单声道 s16le WAV spool（与 PcmSpool 的文件格式一致）。
// 追加写、随时 fflush 后发布 committed；RIFF 长度字段不回填（读者以 committed 为准，不信任头部）。

#include "test_paths.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

class WavSpoolBuilder {
public:
    ~WavSpoolBuilder() { close(); }

    bool create(const std::string &path)
    {
        close();
        // 路径是 UTF-8：Windows 上窄字符 fopen 走 ANSI 代码页，非 ASCII 路径要经 _wfopen
        m_file = testpaths::fopenUtf8(path, "wb");
        if (!m_file)
            return false;
        unsigned char hdr[44];
        constexpr unsigned char base[44] = {
            'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
            16, 0, 0, 0, 1, 0, 1, 0, 0x80, 0x3E, 0, 0, 0, 0x7D, 0, 0, 2, 0, 16, 0,
            'd', 'a', 't', 'a', 0xFF, 0xFF, 0xFF, 0xFF};
        for (int i = 0; i < 44; ++i)
            hdr[i] = base[i];
        std::fwrite(hdr, 1, sizeof hdr, m_file);
        std::fflush(m_file);
        m_written = 0;
        m_phase = 0.0;
        return true;
    }

    void appendSpeechMs(double ms, double amp = 0.8, double freqHz = 440.0)
    {
        const int64_t n = static_cast<int64_t>(ms * 16.0);
        for (int64_t i = 0; i < n; ++i) {
            m_phase += freqHz / 16000.0;
            appendSample(amp * sin2pi(m_phase));
        }
    }

    void appendSilenceMs(double ms) { appendSpeechMs(ms, 0.0, 0.0); }

    void flush() { std::fflush(m_file); }

    int64_t writtenSamples() const { return m_written; }
    void close()
    {
        if (m_file) {
            std::fclose(m_file);
            m_file = nullptr;
        }
    }

private:
    static double sin2pi(double phase) { return std::sin(6.283185307179586 * phase); }
    void appendSample(float v)
    {
        int s = static_cast<int>(v * 32767.0);
        if (s > 32767)
            s = 32767;
        if (s < -32768)
            s = -32768;
        const unsigned char b[2] = {static_cast<unsigned char>(s & 0xFF),
                                    static_cast<unsigned char>((s >> 8) & 0xFF)};
        std::fwrite(b, 1, 2, m_file);
        ++m_written;
    }

    std::FILE *m_file = nullptr;
    int64_t m_written = 0;
    double m_phase = 0.0;
};

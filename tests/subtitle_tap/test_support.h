#pragma once
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#if SUBREC_TAP_HAS_FFMPEG
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}
#else
// Opaque test value: the header-free fallback must never inspect AVFrame fields.
struct AVFrame {};
#endif

#define CHECK(expr) do { if (!(expr)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #expr); } while (false)

template<class Predicate> void eventually(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
#if SUBREC_TAP_HAS_FFMPEG
struct Frame {
    AVFrame av{};
    std::vector<float> first, second;
    Frame(int n, int channels = 1, bool planar = false)
        : first(size_t(n) * (planar ? 1 : channels)), second(planar && channels == 2 ? n : 0)
    {
        av.nb_samples = n;
        av.sample_rate = 48000;
        av.ch_layout.nb_channels = channels;
        av.format = planar ? AV_SAMPLE_FMT_FLTP : AV_SAMPLE_FMT_FLT;
        av.pts = 0;
        av.data[0] = reinterpret_cast<uint8_t *>(first.data());
        av.data[1] = reinterpret_cast<uint8_t *>(second.data());
        av.extended_data = av.data;
    }
};
#endif

#include "audio_mix_dump.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <locale>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/log.h>
}
namespace {
constexpr int maxSamples = 480;
constexpr int bytesPerSampleFrame = 2 * 4;
constexpr size_t maxQueuedBlocks = 200; // 2 s at 10 ms/block; never block capture on I/O
const char *names[] = {"system", "microphone"};
std::atomic<uint64_t> nextSession{0};
}
struct AudioMixDump::Impl {
    struct Block {
        int samples;
        int64_t capturePts;
        std::array<std::array<char, maxSamples * bytesPerSampleFrame>, 2> bytes;
    };
    struct Segment { int64_t fileSample, capturePts, samples; };
    std::filesystem::path base, directory;
    int64_t startUs;
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<Block> queue;
    bool stopping = false;
    std::atomic<bool> failed{false};
    std::thread worker;
    std::ofstream audio[2];
    std::vector<Segment> segments;
    int64_t written = 0;

    Impl(std::filesystem::path path, int64_t start) : base(std::move(path)), startUs(start) {}
    void fail() noexcept {
        if (!failed.exchange(true))
            av_log(nullptr, AV_LOG_WARNING, "[AudioMixDump] disabled after I/O error or queue overflow; recording continues\n");
        cv.notify_one();
    }
    void metadata(bool complete) {
        for (int source = 0; source < 2; ++source) {
            std::ofstream out(directory / (std::string(names[source]) + ".json"), std::ios::trunc);
            out.imbue(std::locale::classic());
            out << "{\n  \"version\": 1,\n  \"source\": \"" << names[source]
                << "\",\n  \"file\": \"" << names[source] << ".f32le\",\n"
                << "  \"format\": \"f32le\",\n  \"interleaved\": true,\n"
                << "  \"sample_rate\": 48000,\n  \"channels\": 2,\n  \"channel_layout\": \"stereo\",\n"
                << "  \"time_base\": [1, 48000],\n  \"start_pts\": 0,\n"
                << "  \"offset_from_mix_output_pts_zero_samples\": 0,\n"
                << "  \"capture_clock_start_us\": " << startUs << ",\n"
                << "  \"capture_timeline_start_pts\": "
                << (segments.empty() ? "null" : std::to_string(segments.front().capturePts)) << ",\n"
                << "  \"sample_frames\": " << written << ",\n"
                << "  \"payload_bytes\": " << written * bytesPerSampleFrame << ",\n"
                << "  \"complete\": " << (complete ? "true" : "false") << ",\n"
                << "  \"tap\": \"aligned_post_gain_pre_amix_pre_limiter\",\n"
                << "  \"timeline\": \"recorded_output_samples_pause_intervals_removed\",\n"
                << "  \"capture_segments\": [";
            for (size_t i = 0; i < segments.size(); ++i) {
                const auto &s = segments[i];
                if (i) out << ',';
                out << "\n    {\"file_sample\": " << s.fileSample << ", \"capture_pts\": " << s.capturePts
                    << ", \"samples\": " << s.samples << '}';
            }
            out << "\n  ]\n}\n";
            out.close();
            if (!out) throw std::runtime_error("dump metadata write failed");
        }
    }
    void run() noexcept {
        try {
            std::filesystem::create_directories(base);
            // One directory per session, including repeated sessions with the same
            // clock epoch. create_directory is exclusive; never overwrite old dumps.
            for (;;) {
                directory = base / ("mix-" + std::to_string(startUs) + "-" + std::to_string(nextSession++));
                if (std::filesystem::create_directory(directory)) break;
            }
            for (int i = 0; i < 2; ++i) {
                audio[i].open(directory / (std::string(names[i]) + ".f32le"), std::ios::binary | std::ios::trunc);
                if (!audio[i]) throw std::runtime_error("dump open failed");
            }
            metadata(false); // Remains incomplete if the process crashes.
            for (;;) {
                Block block;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    cv.wait(lock, [&] { return stopping || failed || !queue.empty(); });
                    if (failed || (queue.empty() && stopping)) break;
                    block = std::move(queue.front()); queue.pop_front();
                }
                // Allocate metadata before writing: allocation failure cannot leave
                // an apparently complete pair with an unrecorded timeline segment.
                const bool extend = !segments.empty() &&
                    segments.back().capturePts + segments.back().samples == block.capturePts;
                if (!extend) segments.reserve(segments.size() + 1);
                for (int i = 0; i < 2; ++i) {
                    audio[i].write(block.bytes[i].data(), block.samples * bytesPerSampleFrame);
                    audio[i].flush();
                    if (!audio[i]) throw std::runtime_error("dump write failed");
                }
                if (extend) segments.back().samples += block.samples;
                else segments.push_back({written, block.capturePts, block.samples});
                written += block.samples;
            }
        } catch (...) { fail(); }
        // A partial pair is not part of the valid dump. Keep both files at the
        // last successfully written common sample count whenever the FS allows it.
        for (int i = 0; i < 2; ++i) {
            if (audio[i].is_open()) {
                audio[i].close();
                if (!audio[i]) fail();
                std::error_code error;
                std::filesystem::resize_file(directory / (std::string(names[i]) + ".f32le"),
                                             written * bytesPerSampleFrame, error);
                if (error) fail();
            }
        }
        if (!directory.empty()) {
            try { metadata(!failed); }
            catch (...) {
                fail();
                // Best effort: retract a complete=true sidecar if writing its
                // partner failed. A failing filesystem may prevent even this.
                try { metadata(false); } catch (...) {}
            }
        }
    }
};
AudioMixDump::AudioMixDump(std::unique_ptr<Impl> impl) : d(std::move(impl)) {
    d->worker = std::thread([this] { d->run(); });
}
std::unique_ptr<AudioMixDump> AudioMixDump::fromEnvironment(int64_t startUs) noexcept {
    try {
#ifdef _WIN32
        const auto *path = _wgetenv(L"SUBREC_AUDIO_MIX_DUMP_DIR"); // Unicode Windows paths
#else
        const auto *path = std::getenv("SUBREC_AUDIO_MIX_DUMP_DIR");
#endif
        if (!path || !*path) return {};
        return std::unique_ptr<AudioMixDump>(new AudioMixDump(std::make_unique<Impl>(path, startUs)));
    } catch (...) {
        av_log(nullptr, AV_LOG_WARNING, "[AudioMixDump] cannot start dump; recording continues\n");
        return {};
    }
}
AudioMixDump::~AudioMixDump() {
    { std::lock_guard<std::mutex> lock(d->mutex); d->stopping = true; }
    d->cv.notify_one();
    if (d->worker.joinable()) d->worker.join();
}
void AudioMixDump::submit(const AVFrame &system, const AVFrame &microphone) noexcept {
    if (d->failed) return;
    try {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
        if (system.nb_samples <= 0 || system.nb_samples > maxSamples ||
            system.nb_samples != microphone.nb_samples || system.pts != microphone.pts)
            throw std::runtime_error("invalid dump pair");
        Impl::Block block{};
        block.samples = system.nb_samples; block.capturePts = system.pts;
        const AVFrame *frames[] = {&system, &microphone};
        for (int source = 0; source < 2; ++source) {
            for (int n = 0; n < block.samples; ++n) {
                for (int ch = 0; ch < 2; ++ch) {
                    uint32_t bits;
                    std::memcpy(&bits, frames[source]->extended_data[ch] + n * sizeof(float), sizeof(bits));
                    auto *dst = block.bytes[source].data() + (n * 2 + ch) * sizeof(float);
                    for (int b = 0; b < 4; ++b) dst[b] = static_cast<char>((bits >> (8*b)) & 0xff);
                }
            }
        }
        { std::lock_guard<std::mutex> lock(d->mutex);
          if (d->queue.size() >= maxQueuedBlocks) { d->fail(); return; }
          d->queue.push_back(std::move(block)); }
        d->cv.notify_one();
    } catch (...) { d->fail(); }
}

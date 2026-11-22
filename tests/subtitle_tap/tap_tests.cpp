#include "test_support.h"
#include "subtitle/tap/audio_tap.h"
#include <atomic>
#include <random>
using namespace subtitle;

void lifecycle()
{
    AudioTap tap(1);
    AudioTap::Block block{};
    CHECK(tap.pop(block) == AudioTap::Pop::Empty);
    tap.markSplice();
    CHECK(tap.spliceRequested());
    tap.consumeSpliceRequest();
    CHECK(!tap.spliceRequested());
    tap.finish(); tap.finish();
    CHECK(tap.pop(block) == AudioTap::Pop::Eof);
    tap.abort(); tap.abort(); tap.finish();
    CHECK(tap.pop(block) == AudioTap::Pop::Aborted);
    tap.release();
#if SUBREC_TAP_HAS_FFMPEG
    Frame frame(1);
    tap.push(frame.av);
#else
    AVFrame frame;
    tap.push(frame);
#endif
    CHECK(tap.stats().pushed == 0 && tap.stats().formatRejects == 0);
    bool rejected = false;
    try { AudioTap invalid(0); } catch (const std::invalid_argument &) { rejected = true; }
    CHECK(rejected);
}

#if SUBREC_TAP_HAS_FFMPEG
void formats()
{
    for (bool planar : {false, true}) for (int channels : {1, 2}) {
        AudioTap tap(1);
        Frame frame(7, channels, planar);
        for (int i = 0; i < 7; ++i) {
            if (channels == 1) frame.first[i] = float(i);
            else if (planar) { frame.first[i] = float(i); frame.second[i] = float(i + 2); }
            else { frame.first[2*i] = float(i); frame.first[2*i+1] = float(i + 2); }
        }
        frame.av.pts = 123;
        tap.push(frame.av);
        // The tap owns a copy, never a retained frame reference.
        std::fill(frame.first.begin(), frame.first.end(), -99.f);
        AudioTap::Block block{};
        CHECK(tap.pop(block) == AudioTap::Pop::Data);
        CHECK(block.samples == 7 && block.pts48k == 123);
        for (int i = 0; i < 7; ++i) CHECK(block.mono[i] == float(i + (channels == 2 ? 1 : 0)));
        tap.release();
    }
    AudioTap tap;
    Frame f(1);
    f.av.sample_rate = 44100; tap.push(f.av); f.av.sample_rate = 48000;
    f.av.format = AV_SAMPLE_FMT_S16; tap.push(f.av); f.av.format = AV_SAMPLE_FMT_FLT;
    f.av.ch_layout.nb_channels = 3; tap.push(f.av); f.av.ch_layout.nb_channels = 1;
    f.av.pts = AV_NOPTS_VALUE; tap.push(f.av); f.av.pts = 0;
    f.av.nb_samples = 0; tap.push(f.av); f.av.nb_samples = 1;
    f.av.data[0] = nullptr; tap.push(f.av);
    CHECK(tap.stats().formatRejects == 6 && tap.stats().pushed == 0);
}
void overflow_wrap()
{
    AudioTap tap(1); // 48 samples
    Frame frame(30);
    for (int i = 0; i < 30; ++i) frame.first[i] = float(i);
    tap.push(frame.av);
    AudioTap::Block block{};
    CHECK(tap.pop(block) == AudioTap::Pop::Data);
    frame.av.pts = 30;
    tap.push(frame.av); // Cannot overwrite a held block.
    CHECK(tap.stats().droppedBlocks == 1 && tap.stats().droppedSamples == 30);
    for (int i = 0; i < 30; ++i) CHECK(block.mono[i] == float(i));
    tap.release();
    frame.av.pts = 60;
    tap.push(frame.av); // Wraps at sample 48; returned samples must be contiguous.
    tap.finish(); tap.finish();
    CHECK(tap.pop(block) == AudioTap::Pop::Data && block.pts48k == 60);
    for (int i = 0; i < 30; ++i) CHECK(block.mono[i] == float(i));
    tap.release();
    CHECK(tap.pop(block) == AudioTap::Pop::Eof);
    tap.push(frame.av);
    CHECK(tap.stats().pushed == 2);
    AudioTap aborted(1);
    aborted.push(frame.av); aborted.finish(); aborted.abort();
    CHECK(aborted.pop(block) == AudioTap::Pop::Aborted);
}
void splice_directions()
{
    AudioTap tap(1);
    Frame frame(48);
    AudioTap::Block b{};
    for (int direction = 0; direction < 2; ++direction) {
        tap.push(frame.av); // Fill before marking: a rejected block must not eat the mark.
        tap.markSplice();
        tap.push(frame.av);
        CHECK(tap.spliceRequested());
        CHECK(tap.pop(b) == AudioTap::Pop::Data && !b.splice); tap.release();
        tap.push(frame.av);
        CHECK(!tap.spliceRequested());
        CHECK(tap.pop(b) == AudioTap::Pop::Data && b.splice); tap.release();
    }
    CHECK(tap.stats().splices == 2 && tap.stats().droppedBlocks == 2);
    tap.markSplice(); CHECK(tap.spliceRequested());
    tap.markSplice(); tap.consumeSpliceRequest();
    CHECK(tap.spliceRequested()); // A delayed clear cannot erase the newer mark.
    tap.consumeSpliceRequest(); CHECK(!tap.spliceRequested());
}
void max_push_ns()
{
    AudioTap tap;
    Frame frame(1024, 2);
    CHECK(tap.stats().maxPushNs == 0);
    uint64_t previous = 0;
    for (int i = 0; i < 100; ++i) {
        tap.push(frame.av);
        CHECK(tap.stats().maxPushNs >= previous && tap.stats().maxPushNs > 0);
        previous = tap.stats().maxPushNs;
    }
    std::cout << "maxPushNs=" << previous << '\n';
}
void stress(bool drops)
{
    constexpr int count = 1000000;
    AudioTap tap(drops ? 1 : 100);
    std::atomic<int> consumed{0};
    std::vector<int64_t> expected, actual;
    expected.reserve(count); actual.reserve(count);
    std::atomic<bool> valid{true};
    std::thread consumer([&] {
        std::minstd_rand random(12345);
        AudioTap::Block b{};
        for (;;) {
            const auto result = tap.pop(b);
            if (result == AudioTap::Pop::Eof) break;
            if (result != AudioTap::Pop::Data) { std::this_thread::yield(); continue; }
            for (int j = 0; j < b.samples; ++j)
                if (b.mono[j] != float(b.pts48k + j)) valid = false;
            actual.push_back(b.pts48k);
            tap.release();
            consumed.fetch_add(1, std::memory_order_release);
            if ((actual.size() % 257) == 0)
                std::this_thread::sleep_for(std::chrono::microseconds(random() % 30));
        }
    });
    Frame frame(7);
    uint64_t accepted = 0, rejectedSamples = 0;
    for (int i = 0; i < count; ++i) {
        if (!drops) while (i - consumed.load(std::memory_order_acquire) >= 512) std::this_thread::yield();
        frame.av.nb_samples = drops ? i % 7 + 1 : 1;
        frame.av.pts = int64_t(i) * 8;
        for (int j = 0; j < frame.av.nb_samples; ++j) frame.first[j] = float(frame.av.pts + j);
        tap.push(frame.av);
        const auto pushed = tap.stats().pushed;
        if (pushed != accepted) { expected.push_back(frame.av.pts); accepted = pushed; }
        else rejectedSamples += uint64_t(frame.av.nb_samples);
    }
    tap.finish();
    consumer.join();
    CHECK(valid.load());
    CHECK(actual == expected);
    CHECK(tap.stats().pushed + tap.stats().droppedBlocks == count);
    CHECK(tap.stats().droppedSamples == rejectedSamples);
    if (!drops) CHECK(actual.size() == count && tap.stats().droppedBlocks == 0);
    else CHECK(tap.stats().droppedBlocks > 0 && !actual.empty());
}
#endif
int main(int argc, char **argv)
{
    try {
        CHECK(argc == 2);
        const std::string test = argv[1];
        if (test == "lifecycle") lifecycle();
#if SUBREC_TAP_HAS_FFMPEG
        else if (test == "formats") formats();
        else if (test == "overflow_wrap") overflow_wrap();
        else if (test == "splice_directions") splice_directions();
        else if (test == "max_push_ns") max_push_ns();
        else if (test == "spsc_million") stress(false);
        else if (test == "spsc_million_drops") stress(true);
#else
        else if (test == "fallback_rejects") {
            AudioTap tap; AVFrame f; tap.push(f);
            CHECK(tap.stats().formatRejects == 1 && tap.stats().maxPushNs > 0);
        }
#endif
        else CHECK(false);
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

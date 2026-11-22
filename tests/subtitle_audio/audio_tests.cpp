#include "../subtitle_tap/test_support.h"
#include "subtitle/audio/ingest_worker.h"
#include "subtitle/audio/mono_resampler_16k.h"
#include "subtitle/audio/pcm_spool.h"
#include "subtitle/tap/audio_tap.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <numeric>
#if defined(__unix__)
#include <csignal>
#include <sys/resource.h>
#endif
using namespace subtitle;
using namespace std::chrono_literals;

struct Temp {
    std::filesystem::path dir;
    explicit Temp(const std::string &name) {
        dir = std::filesystem::temp_directory_path() / ("subrec-a-" + name + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(dir);
    }
    ~Temp() { std::error_code error; std::filesystem::remove_all(dir, error); }
    std::filesystem::path wav() const { return dir / "audio.wav"; }
};
std::vector<unsigned char> read(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    CHECK(stream.good());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
uint32_t u32(const std::vector<unsigned char> &bytes, size_t at)
{
    CHECK(bytes.size() >= at + 4);
    return uint32_t(bytes[at]) | uint32_t(bytes[at+1]) << 8 | uint32_t(bytes[at+2]) << 16 | uint32_t(bytes[at+3]) << 24;
}
int sample(const std::vector<unsigned char> &bytes, size_t index)
{
    const size_t at = 44 + 2 * index;
    CHECK(bytes.size() >= at + 2);
    const int value = bytes[at] | int(bytes[at+1]) << 8;
    return value >= 32768 ? value - 65536 : value;
}
struct AtomicClock : IClock {
    std::atomic<int64_t> now{0};
    int64_t nowMs() const override { return now.load(); }
};

void fir_impulse()
{
    for (int position : {0, 1, 2, 299, 300, 301, 302, 959}) {
        std::vector<float> input(960), out{42.f};
        input[position] = 1.f;
        MonoResampler16k rs;
        for (size_t at = 0; at < input.size();) {
            const size_t n = std::min<size_t>(1 + at % 37, input.size() - at);
            rs.process(input.data() + at, n, out); at += n;
        }
        rs.flush(out);
        CHECK(out.front() == 42.f && out.size() == 321);
        const auto peak = std::max_element(out.begin() + 1, out.end()) - out.begin() - 1;
        CHECK(std::abs(double(peak) - double(position) / 3) <= 1.0);
        const size_t before = out.size(); rs.flush(out); CHECK(out.size() == before);
    }
}
void fir_chunks_filter()
{
    for (size_t count : {size_t(1), size_t(2), size_t(3), size_t(29), size_t(30), size_t(31), size_t(10001)}) {
        std::vector<float> input(count), whole, chunks;
        for (size_t i = 0; i < count; ++i) input[i] = float(std::sin(i * .17));
        MonoResampler16k a, b;
        a.process(input.data(), count, whole); a.flush(whole);
        for (size_t i = 0; i < count; ++i) b.process(&input[i], 1, chunks);
        b.flush(chunks);
        CHECK(whole == chunks && whole.size() == (count + 2) / 3);
        b.process(input.data(), count, chunks); b.reset();
        chunks.clear(); b.process(input.data(), count, chunks); b.flush(chunks);
        CHECK(whole == chunks);
    }
    auto rms = [](double hz) {
        std::vector<float> input(48000), output;
        for (size_t i = 0; i < input.size(); ++i) input[i] = float(std::sin(2 * 3.141592653589793 * hz * i / 48000));
        MonoResampler16k rs; rs.process(input.data(), input.size(), output); rs.flush(output);
        double sum = 0;
        for (size_t i = 100; i < output.size() - 100; ++i) sum += output[i] * output[i];
        return std::sqrt(sum / (output.size() - 200));
    };
    CHECK(rms(1000) > .69);
    CHECK(rms(12000) < .005);
}
void spool_roundtrip()
{
    Temp temp("roundtrip");
    PcmSpool spool;
    CHECK(spool.create(temp.wav()).ok());
    const float input[] = {-2.f, -1.f, -.5f, 0.f, .5f, 1.f, 2.f,
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
    CHECK(spool.append(input, 4).ok());
    CHECK(spool.append(input + 4, 5).ok());
    CHECK(spool.writtenSamples() == 9 && spool.committedSamples() == 0);
    spool.publish(9);
    auto bytes = read(temp.wav()); // Independent reader sees data before finalize.
    CHECK(bytes.size() == 62 && u32(bytes, 40) == 0);
    CHECK(spool.finalize().ok() && spool.finalize().ok());
    CHECK(!spool.append(input, 1).ok());
    bytes = read(temp.wav());
    CHECK(std::string(bytes.begin(), bytes.begin() + 4) == "RIFF");
    CHECK(std::string(bytes.begin() + 8, bytes.begin() + 16) == "WAVEfmt ");
    CHECK(u32(bytes, 4) == 54 && u32(bytes, 16) == 16);
    CHECK(bytes[20] == 1 && bytes[22] == 1 && u32(bytes, 24) == 16000);
    CHECK(u32(bytes, 28) == 32000 && bytes[32] == 2 && bytes[34] == 16);
    CHECK(std::string(bytes.begin() + 36, bytes.begin() + 40) == "data" && u32(bytes, 40) == 18);
    const int expected[] = {-32768, -32768, -16384, 0, 16384, 32767, 32767, 0, 32767};
    for (size_t i = 0; i < 9; ++i) CHECK(sample(bytes, i) == expected[i]);
    CHECK(spool.path() == temp.wav());
}
void publish_monotonic()
{
    Temp temp("publish"); PcmSpool spool;
    CHECK(spool.create(temp.wav()).ok());
    spool.publish(160); CHECK(spool.committedSamples() == 0);
    std::atomic<bool> done{false}, valid{true};
    std::thread reader([&] {
        int64_t previous = 0;
        while (!done.load()) {
            const auto next = spool.committedSamples();
            if (next < previous || next > 10000) valid = false;
            previous = next;
        }
    });
    float zero = 0;
    bool writerValid = true;
    for (int i = 1; i <= 10000; ++i) {
        if (!spool.append(&zero, 1).ok()) writerValid = false;
        spool.publish(std::numeric_limits<int64_t>::max());
        spool.publish(-100); spool.publish(i - 1);
        if (spool.committedSamples() != i || spool.committedSamples() > spool.writtenSamples()) writerValid = false;
    }
    done = true; reader.join();
    CHECK(writerValid && valid.load());
    spool.markSplice16k(100); spool.markSplice16k(200);
    CHECK(spool.takeSplice() == 200 && spool.takeSplice() == -1);
    CHECK(spool.finalize().ok());
}
void worker_lifecycle()
{
    Temp temp("lifecycle");
    AtomicClock clock;
    auto spool = std::make_shared<PcmSpool>(); CHECK(spool->create(temp.wav()).ok());
    auto tap = std::make_shared<AudioTap>(1);
    {
        IngestWorker worker(tap, spool, clock);
        CHECK(worker.join(0ms)); CHECK(worker.start().ok()); CHECK(!worker.start().ok());
        CHECK(!worker.join(1ms));
        worker.requestStop(); CHECK(worker.join(1s) && worker.join(0ms));
        CHECK(worker.lastError().ok());
    }
    CHECK(u32(read(temp.wav()), 4) == 36);
    tap->finish();
    { IngestWorker worker(tap, spool, clock); CHECK(worker.start().ok()); CHECK(worker.join(1s)); CHECK(worker.lastError().ok()); }
    CHECK(read(temp.wav()).size() == 44);
    IngestWorker invalid(nullptr, spool, clock); CHECK(!invalid.start().ok());
    auto idle = std::make_shared<AudioTap>(1);
    { IngestWorker worker(idle, spool, clock); CHECK(worker.start().ok()); } // Destructor stops and joins.
}
void idle_splice()
{
    Temp temp("idle"); AtomicClock clock;
    auto spool = std::make_shared<PcmSpool>(); CHECK(spool->create(temp.wav()).ok());
    std::vector<float> zeros(160); CHECK(spool->append(zeros.data(), zeros.size()).ok()); spool->publish(160);
    auto tap = std::make_shared<AudioTap>(1);
    IngestWorker worker(tap, spool, clock); CHECK(worker.start().ok());
    for (int direction = 1; direction <= 2; ++direction) {
        tap->markSplice();
        eventually([&] { return worker.stats().splices == uint64_t(direction); });
        CHECK(spool->takeSplice() == 160 && !tap->spliceRequested());
    }
    worker.requestStop(); CHECK(worker.join(1s));
}
#if SUBREC_TAP_HAS_FFMPEG
void pts_gaps()
{
    Temp temp("gaps"); AtomicClock clock;
    auto spool = std::make_shared<PcmSpool>(); CHECK(spool->create(temp.wav()).ok());
    auto tap = std::make_shared<AudioTap>(100);
    Frame frame(480); std::fill(frame.first.begin(), frame.first.end(), .5f);
    frame.av.pts = 480; tap->push(frame.av); // Initial hole 480.
    frame.av.pts = 1920; tap->push(frame.av); // Internal hole 960.
    tap->finish();
    IngestWorker worker(tap, spool, clock); CHECK(worker.start().ok()); CHECK(worker.join(2s));
    CHECK(worker.lastError().ok());
    const auto stats = worker.stats();
    CHECK(stats.blocks == 2 && stats.gaps == 2 && stats.gapSamples == 1440);
    CHECK(spool->writtenSamples() == 800 && spool->committedSamples() == 800);
    const auto bytes = read(temp.wav()); CHECK(u32(bytes, 40) == 1600);
    for (size_t i = 0; i < 145; ++i) CHECK(sample(bytes, i) == 0);
    for (size_t i = 340; i < 620; ++i) CHECK(sample(bytes, i) == 0);
    CHECK(std::abs(sample(bytes, 200) - 16384) <= 1);
    CHECK(std::abs(sample(bytes, 700) - 16384) <= 1);
}
void worker_publish_splice()
{
    Temp temp("cadence"); AtomicClock clock;
    auto spool = std::make_shared<PcmSpool>(); CHECK(spool->create(temp.wav()).ok());
    auto tap = std::make_shared<AudioTap>(100);
    Frame frame(480); tap->push(frame.av);
    IngestWorker worker(tap, spool, clock); CHECK(worker.start().ok());
    eventually([&] { return worker.stats().blocks == 1; });
    CHECK(spool->committedSamples() == 0);
    clock.now = 99; std::this_thread::sleep_for(5ms); CHECK(spool->committedSamples() == 0);
    clock.now = 100; eventually([&] { return spool->committedSamples() == 150; });
    tap->markSplice(); // Pause with no further audio, nonzero commit.
    eventually([&] { return worker.stats().splices == 1; });
    CHECK(spool->takeSplice() == 150);
    // Start a second worker only after the first has stopped to queue a marked
    // resume block without an idle consumer consuming its mark first.
    worker.requestStop(); CHECK(worker.join(1s));
    auto resumedTap = std::make_shared<AudioTap>(100);
    auto resumedSpool = std::make_shared<PcmSpool>(); CHECK(resumedSpool->create(temp.dir / "resume.wav").ok());
    frame.av.pts = 480; resumedTap->markSplice(); resumedTap->push(frame.av); resumedTap->finish();
    IngestWorker resumed(resumedTap, resumedSpool, clock); CHECK(resumed.start().ok()); CHECK(resumed.join(1s));
    CHECK(resumedSpool->takeSplice() == 160 && resumed.stats().splices == 1);
}
void abort_no_finalize()
{
    Temp temp("abort"); AtomicClock clock;
    auto spool = std::make_shared<PcmSpool>(); CHECK(spool->create(temp.wav()).ok());
    auto tap = std::make_shared<AudioTap>(100); Frame frame(480); tap->push(frame.av);
    IngestWorker worker(tap, spool, clock); CHECK(worker.start().ok());
    eventually([&] { return worker.stats().blocks == 1; });
    clock.now = 100; eventually([&] { return spool->committedSamples() > 0; });
    tap->abort(); CHECK(worker.join(1s)); CHECK(worker.lastError().ok());
    const auto bytes = read(temp.wav()); CHECK(bytes.size() > 44 && u32(bytes, 40) == 0);
}
#if defined(__unix__)
void disk_failure_drain()
{
    Temp temp("disk"); AtomicClock clock;
    auto spool = std::make_shared<PcmSpool>(); CHECK(spool->create(temp.wav()).ok());
    struct FileLimit {
        rlimit previous{};
        using Handler = void (*)(int);
        Handler handler;
        FileLimit() {
            CHECK(getrlimit(RLIMIT_FSIZE, &previous) == 0);
            handler = std::signal(SIGXFSZ, SIG_IGN);
            auto limit = previous; limit.rlim_cur = 344;
            CHECK(setrlimit(RLIMIT_FSIZE, &limit) == 0);
        }
        ~FileLimit() { setrlimit(RLIMIT_FSIZE, &previous); std::signal(SIGXFSZ, handler); }
    } limit;
    auto tap = std::make_shared<AudioTap>(1000);
    Frame frame(480);
    for (int i = 0; i < 10; ++i) { frame.av.pts = i * 480; tap->push(frame.av); }
    IngestWorker worker(tap, spool, clock); CHECK(worker.start().ok());
    eventually([&] { return !worker.lastError().ok() && worker.stats().blocks == 10; });
    CHECK(worker.lastError().code == ErrorCode::SpoolWriteFailed);
    for (int i = 10; i < 20; ++i) { frame.av.pts = i * 480; tap->push(frame.av); }
    tap->finish(); CHECK(worker.join(2s));
    CHECK(worker.stats().blocks == 20 && tap->stats().droppedBlocks == 0);
    CHECK(spool->writtenSamples() <= 150 && spool->committedSamples() <= spool->writtenSamples());
    CHECK(u32(read(temp.wav()), 40) == 0); // Failed spool was never finalized.
}
#endif
#endif
int main(int argc, char **argv)
{
    try {
        CHECK(argc == 2); const std::string test = argv[1];
        if (test == "fir_impulse") fir_impulse();
        else if (test == "fir_chunks_filter") fir_chunks_filter();
        else if (test == "spool_roundtrip") spool_roundtrip();
        else if (test == "publish_monotonic") publish_monotonic();
        else if (test == "worker_lifecycle") worker_lifecycle();
        else if (test == "idle_splice") idle_splice();
#if SUBREC_TAP_HAS_FFMPEG
        else if (test == "pts_gaps") pts_gaps();
        else if (test == "worker_publish_splice") worker_publish_splice();
        else if (test == "abort_no_finalize") abort_no_finalize();
#if defined(__unix__)
        else if (test == "disk_failure_drain") disk_failure_drain();
#endif
#endif
        else CHECK(false);
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

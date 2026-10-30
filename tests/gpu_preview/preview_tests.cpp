#include "capture/latest_preview_slot.h"
#include "capture/gpu_preview.h"
#include <atomic>
#include <cstdio>
#include <memory>
#include <thread>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
struct Tracked {
    static inline std::atomic<int> live{0};
    int sequence;
    explicit Tracked(int n) : sequence(n) { ++live; }
    ~Tracked() { --live; }
};
int main() {
    try {
        LatestPreviewSlot<std::unique_ptr<Tracked>> slot;
        CHECK(slot.offer(std::make_unique<Tracked>(0)));
        for (int i=1; i<=100000; ++i) {
            CHECK(!slot.offer(std::make_unique<Tracked>(i)));
            CHECK(Tracked::live == 1); // stalled GUI/consumer cannot accumulate frames
        }
        auto latest = slot.take(); CHECK(latest && latest->sequence == 100000);
        CHECK(!slot.take()); latest.reset(); CHECK(Tracked::live == 0);
        std::atomic<bool> done{false};
        std::thread producer([&] {
            for (int i=0; i<100000; ++i) slot.offer(std::make_unique<Tracked>(i));
            done = true;
        });
        int previous = -1;
        bool ordered = true;
        while (!done) {
            auto frame = slot.take();
            if (frame) { if (frame->sequence <= previous) ordered = false; previous = frame->sequence; }
        }
        producer.join();
        CHECK(ordered); slot.take().reset(); CHECK(Tracked::live == 0);
        const auto gpu = gpuPreviewFilterSpec();
        CHECK(gpu.find("fps=8,hwdownload,format=bgra,") == 0);
        CHECK(gpu.find("format=yuv420p") != std::string::npos);
        CHECK(gpuPreviewFilterSpec(false).find("hwdownload") == std::string::npos);
        std::puts("PASS latest slot: 100000 overwrites, concurrent producer/consumer, no retained frames");
    } catch (const std::exception &e) { std::fprintf(stderr,"FAIL %s\n", e.what()); return 1; }
}

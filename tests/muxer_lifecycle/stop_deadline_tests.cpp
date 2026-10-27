#include "thread/ffthread.h"
#include "thread/stop_deadline.h"
#include "queue/ffapacketqueue.h"
#include <future>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
struct Worker : FFThread {
    std::function<void()> action;
    void run() override { action(); }
    // Tests always release native-call gates and join before destroying the fake.
    ~Worker() override { wait(); }
};
void packet(FFAPacketQueue &queue, int64_t pts, int &freed) {
    auto *p = av_packet_alloc(); CHECK(p);
    p->buf = av_buffer_create(static_cast<uint8_t *>(av_malloc(1)), 1,
        [](void *opaque, uint8_t *data) { ++*static_cast<int *>(opaque); av_free(data); }, &freed, 0);
    CHECK(p->buf); p->data = p->buf->data; p->size = 1; p->pts = p->dts = pts;
    queue.enqueue(p); av_packet_free(&p);
}
void configuredTimeout() {
    const char *old = std::getenv("SUBREC_STOP_TIMEOUT_MS");
    const std::string saved = old ? old : "";
    auto set = [](const char *value) {
#ifdef _WIN32
        CHECK(_putenv_s("SUBREC_STOP_TIMEOUT_MS", value) == 0);
#else
        CHECK(setenv("SUBREC_STOP_TIMEOUT_MS", value, 1) == 0);
#endif
    };
    for (const char *invalid : {"", "0", "-1", "60001", "garbage", "80x"}) {
        set(invalid); CHECK(capture_stop::timeout() == 5s);
    }
    set("80"); CHECK(capture_stop::timeout() == 80ms);
    set(saved.c_str());
    std::cout << "PASS: configurable stop timeout, invalid values fall back to 5000 ms\n";
}
void queueStalls(bool producer) {
    FFAPacketQueue queue; queue.start();
    int freed = 0;
    if (producer) for (int i = 0; i < 30; ++i) packet(queue, i, freed);
    Worker worker;
    std::promise<void> entered;
    bool exited = false;
    worker.action = [&] {
        entered.set_value();
        if (producer) packet(queue, 30, freed); // blocked full output
        else {
            auto result = queue.receive(); // stalled upstream, no EOF
            CHECK(result.result == MediaQueueResult::Closed);
        }
        exited = true;
    };
    worker.start(); entered.get_future().wait();
    int logs = 0;
    const auto start = std::chrono::steady_clock::now();
    CHECK(capture_stop::drain(worker, [&] {
        worker.stop();
        if (producer) queue.sendFinish(); // wake producer, retain existing packets
        else queue.close();
    }, [&](const char *) { ++logs; }, 80ms));
    const auto ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    CHECK(exited && logs == 1 && ms >= 70 && ms < 1000);
    if (producer) {
        CHECK(freed == 1); // rejected packet released, queued packets not discarded
        for (int i = 0; i < 30; ++i) {
            auto result = queue.receive(); CHECK(result.ok() && result.item->packet.pts == i);
            FFPacketTraits::release(result.item);
        }
        CHECK(queue.receive().result == MediaQueueResult::Eof && freed == 31);
    }
    CHECK(worker.waitFor(0ms));
    std::cout << "PASS: " << (producer ? "full producer" : "stalled consumer")
              << " stopped/joined ms=" << ms << " logs=" << logs << '\n';
}
void retainUntilJoined() {
    Worker worker;
    std::promise<void> entered, release;
    auto gate = release.get_future();
    int alive = 1;
    worker.action = [&] { entered.set_value(); gate.wait(); CHECK(alive == 1); };
    worker.start(); entered.get_future().wait();
    int logs = 0;
    CHECK(!capture_stop::drain(worker, [&] { worker.stop(); }, [&](const char *) { ++logs; }, 20ms));
    CHECK(logs == 2); // Stop flag is not a completed-thread notification.
    release.set_value(); CHECK(worker.waitFor(1s)); alive = 0;
    // Completion tracking also works for reuse and threads that never started.
    worker.action = [] {}; worker.start(); CHECK(worker.waitFor(1s));
    Worker unused; CHECK(unused.waitFor(0ms));
    std::cout << "PASS: second timeout retains ownership, subsequent join/restart succeeds\n";
}
int main() {
    try {
        configuredTimeout(); queueStalls(false); queueStalls(true); retainUntilJoined();
    } catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

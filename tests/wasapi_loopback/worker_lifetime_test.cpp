#include "capture/wasapi_worker_lifetime.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace capture::wasapi;
using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__)); } while (false)
struct Gate {
    std::mutex mutex;
    std::condition_variable cv;
    bool released = false;
    void wait() { std::unique_lock<std::mutex> l(mutex); cv.wait(l, [&] { return released; }); }
    void release() { std::lock_guard<std::mutex> l(mutex); released = true; cv.notify_all(); }
};
struct State {
    std::shared_ptr<Gate> gate;
    std::atomic<bool> abort{false}, stop{false};
    std::atomic<int> &destroyed;
    explicit State(std::shared_ptr<Gate> g, std::atomic<int> &d) : gate(std::move(g)), destroyed(d) {}
    ~State() { ++destroyed; }
};
// Fake non-interruptible device call. Uses the same supervisor, owned-state task,
// preparation handshake and two-stage stop policy as LoopbackCapture.
struct FakeCapture {
    std::shared_ptr<State> state;
    WorkerLifetime worker;
    FakeCapture(std::shared_ptr<Gate> gate, std::atomic<int> &destroyed)
        : state(std::make_shared<State>(std::move(gate), destroyed)) {}
    ~FakeCapture() { requestAbort(); if (!joinFor(20ms)) abandon(); }
    bool open(bool blockInOpen) {
        worker.start([s = state, signal = worker.completion(), blockInOpen] {
            if (blockInOpen) s->gate->wait();
            signal->prepared();
            if (!blockInOpen) s->gate->wait();
            // Touch owned state after the caller/owner has been destroyed (ASan).
            CHECK(s->gate && (s->abort || s->stop));
        });
        if (worker.waitPrepared(20ms)) return true;
        abandon(); return false;
    }
    void requestDrainStop() { state->stop = true; }
    void requestAbort() { state->abort = true; }
    bool joinFor(std::chrono::milliseconds timeout) { return worker.joinFor(timeout); }
    void abandon() { requestAbort(); worker.abandon(); }
};
void completed(const std::shared_ptr<WorkerLifetime::Signals> &completion) {
    std::unique_lock<std::mutex> l(completion->mutex);
    CHECK(completion->cv.wait_for(l, 2s, [&] { return completion->done; }));
    CHECK(!completion->error);
}
double ms(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count();
}
int main() {
    try {
        {
            auto gate = std::make_shared<Gate>(); std::atomic<int> destroyed{0};
            std::weak_ptr<State> weak;
            std::shared_ptr<WorkerLifetime::Signals> completion;
            const auto start = std::chrono::steady_clock::now();
            {
                FakeCapture capture(gate, destroyed); weak = capture.state; completion = capture.worker.completion();
                CHECK(!capture.open(true)); CHECK(completion->timedOut());
                bool rejected = false;
                try { capture.worker.start([] {}); } catch (const std::logic_error &) { rejected = true; }
                CHECK(rejected); // detach does not permit reopening this instance
                CHECK(!capture.joinFor(5s)); // already detached: must not wait again
            }
            const double elapsed = ms(start);
            CHECK(elapsed < 500 && !weak.expired() && destroyed == 0);
            gate->release(); completed(completion);
            CHECK(weak.expired() && destroyed == 1 && completion->timedOut());
            CHECK(!completion->ready); // a late successful open cannot resurrect timeout
            std::cout << "PASS open timeout + destruction ms=" << elapsed << " reclaimed=" << destroyed << '\n';
        }
        for (bool releaseOnClose : {true, false}) {
            auto gate = std::make_shared<Gate>(); std::atomic<int> destroyed{0};
            std::shared_ptr<WorkerLifetime::Signals> completion;
            std::weak_ptr<State> weak;
            int closes = 0, logs = 0;
            const auto start = std::chrono::steady_clock::now();
            {
                FakeCapture capture(gate, destroyed); completion = capture.worker.completion(); weak = capture.state;
                CHECK(capture.open(false));
                const bool joined = stopWithDeadline(capture, [&] {
                    ++closes; if (releaseOnClose) gate->release();
                }, [&](const char *) { ++logs; }, 20ms, 20ms);
                CHECK(joined == releaseOnClose && closes == 1 && logs == (releaseOnClose ? 1 : 2));
            }
            const double elapsed = ms(start); CHECK(elapsed < 500);
            if (!releaseOnClose) CHECK(!weak.expired());
            gate->release(); completed(completion);
            CHECK(weak.expired() && destroyed == 1);
            std::cout << "PASS stop release_on_close=" << releaseOnClose << " ms=" << elapsed
                      << " closes=" << closes << " logs=" << logs << " reclaimed=" << destroyed << '\n';
        }
        {
            auto gate = std::make_shared<Gate>(); std::atomic<int> destroyed{0};
            std::shared_ptr<WorkerLifetime::Signals> completion;
            const auto start = std::chrono::steady_clock::now();
            {
                FakeCapture capture(gate, destroyed); completion = capture.worker.completion();
                CHECK(capture.open(false)); // Destructor itself must bound the wait.
            }
            const double elapsed = ms(start);
            CHECK(elapsed < 500 && completion->timedOut() && destroyed == 0);
            gate->release(); completed(completion); CHECK(destroyed == 1);
            std::cout << "PASS prepared-but-stuck destructor ms=" << elapsed << " reclaimed=" << destroyed << '\n';
        }
        {
            auto gate = std::make_shared<Gate>(); std::atomic<int> destroyed{0};
            FakeCapture capture(gate, destroyed); CHECK(capture.open(false));
            capture.requestDrainStop(); gate->release(); CHECK(capture.joinFor(2s));
            CHECK(!capture.worker.completion()->timedOut());
            CHECK(stopWithDeadline(capture, [] { CHECK(false); }, [](const char *) { CHECK(false); }, 20ms, 20ms));
            std::cout << "PASS normal completion: no forced queue close or detach\n";
        }
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

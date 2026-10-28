#include "capture/wasapi_buffered_sink.h"
#include "capture/wasapi_queue_sink.h"
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace capture::wasapi;
using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__)); } while (false)
struct FrameDelete { void operator()(AVFrame *p) const { av_frame_free(&p); } };
using Frame = std::unique_ptr<AVFrame, FrameDelete>;
Frame frame(int index, int samples = 480) {
    Frame f(av_frame_alloc()); CHECK(f);
    f->format = AV_SAMPLE_FMT_FLTP; f->sample_rate = kOutputSampleRate;
    f->nb_samples = samples; f->pts = int64_t(index)*samples; f->time_base = {1,kOutputSampleRate};
    av_channel_layout_default(&f->ch_layout, 2); CHECK(av_frame_get_buffer(f.get(), 0) == 0);
    CHECK(av_dict_set_int(&f->metadata, "capture_time_us", 1000000 + f->pts*1000000/kOutputSampleRate, 0) == 0);
    for (int ch=0; ch<2; ++ch) for (int n=0; n<samples; ++n)
        reinterpret_cast<float *>(f->data[ch])[n] = float(index);
    return f;
}
// Gate the delivery worker inside push: deliberately worse than a full queue.
// The source must not enter this mutex or depend on this thread being scheduled.
struct GateSink : AudioFrameSink {
    std::promise<void> entered;
    std::mutex mutex; std::condition_variable cv;
    bool released = false, cancelled = false;
    int frames = 0, finishes = 0;
    std::vector<int64_t> pts;
    std::vector<float> values;
    bool push(AVFrame *f) override {
        if (frames++ == 0) entered.set_value();
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return released || cancelled; });
        if (cancelled) return false;
        pts.push_back(f->pts);
        values.push_back(reinterpret_cast<float *>(f->data[0])[0]);
        cv.notify_all();
        av_frame_unref(f); return true;
    }
    void waitFrames(size_t count) {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(cv.wait_for(lock, 1s, [&] { return pts.size() >= count; }));
    }
    void release() { std::lock_guard<std::mutex> lock(mutex); released = true; cv.notify_all(); }
    void finish() override { ++finishes; }
    void cancel() noexcept override { std::lock_guard<std::mutex> lock(mutex); cancelled = true; cv.notify_all(); }
};
void delayedConsumer(bool timestamped) {
    FFAFrameQueue queue;
    auto delivery = std::make_shared<QueueAudioSink>(queue, timestamped);
    BufferedAudioSink sink(delivery);
    const auto start = std::chrono::steady_clock::now();
    uint64_t maxNs = 0;
    // No consumer for 2.4 seconds. Existing 30-frame queue fills at ~300 ms;
    // the capture handoff must keep accepting new PCM until video can start.
    for (int i=0; i<240; ++i) {
        auto f=frame(i);
        const auto before=std::chrono::steady_clock::now();
        CHECK(sink.push(f.get()));
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-before).count();
        maxNs=std::max(maxNs, static_cast<uint64_t>(ns));
        CHECK(f->nb_samples == 0);
        std::this_thread::sleep_until(start + (i+1)*10ms);
    }
    // 1 ms bounds the handoff, NOT device/FFmpeg conversion or scheduler latency.
    CHECK(maxNs < 1000000);
    sink.finish(); CHECK(!sink.joinFor(0ms)); // EOF may not overtake pending audio
    for (int i=0; i<240; ++i) {
        auto result=queue.receive(); Frame f(result.item); CHECK(result.ok());
        CHECK(f->pts == (timestamped ? 1000000+i*10000 : i*480));
        CHECK(f->time_base.den == (timestamped ? 1000000 : kOutputSampleRate));
        CHECK(f->nb_samples == 480);
        for (int ch=0; ch<2; ++ch) for (int n=0; n<480; ++n)
            CHECK(reinterpret_cast<float *>(f->data[ch])[n] == float(i));
    }
    CHECK(queue.receive().result == MediaQueueResult::Eof && sink.joinFor(1s));
    const auto stats=sink.diagnostics();
    CHECK(stats.blockedNs == 0 && stats.maxBlockedNs == 0 && stats.downstreamDroppedSamples == 0);
    CHECK(stats.downstreamDroppedFrames == 0 && stats.deliveryBlockedNs > 2000000000ULL);
    std::cout << "PASS: delayed consumer 2400 ms mix=" << timestamped << " samples=115200 lost=0 push_max_us="
              << maxNs/1000.0 << " capture_blocked_ms=0 delivery_blocked_ms=" << stats.deliveryBlockedNs/1000000.0 << '\n';
}
void overflow(bool bySamples) {
    auto gate=std::make_shared<GateSink>();
    BufferedAudioSink sink(gate, bySamples ? 16 : 2, bySamples ? 960 : 48000);
    auto first=frame(0); CHECK(sink.push(first.get())); gate->entered.get_future().wait();
    // In-flight frame counts against sample cap; slot cap excludes in-flight.
    const int accepted=bySamples ? 2 : 3;
    uint64_t maxPushNs = 0;
    for (int i=1; i<6; ++i) {
        auto f=frame(i); const auto start=std::chrono::steady_clock::now();
        CHECK(sink.push(f.get()));
        maxPushNs=std::max(maxPushNs, static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-start).count()));
    }
    CHECK(maxPushNs < 1000000);
    auto stats=sink.diagnostics();
    CHECK(stats.downstreamDroppedSamples == uint64_t(6-accepted)*480);
    CHECK(stats.downstreamDroppedFrames == uint64_t(6-accepted));
    if (bySamples) {
        const auto start=std::chrono::steady_clock::now(); sink.finish();
        CHECK(std::chrono::steady_clock::now()-start < 1ms && !sink.joinFor(0ms));
    }
    gate->release();
    if (!bySamples) {
        gate->waitFrames(accepted);
        auto recovered=frame(6); CHECK(sink.push(recovered.get()) && recovered->nb_samples == 0);
    }
    sink.finish(); CHECK(sink.joinFor(1s));
    const int total=bySamples ? 6 : 7;
    CHECK(gate->pts.size() == size_t(total) && gate->finishes == 1);
    for (int i=0; i<total; ++i) {
        CHECK(gate->pts[i] == i*480);
        CHECK(gate->values[i] == (i<accepted || i==6 ? float(i) : 0.0f));
    }
    CHECK(sink.diagnostics().downstreamSilenceSamples == uint64_t(6-accepted)*480);
    CHECK(sink.diagnostics().downstreamDroppedSamples == stats.downstreamDroppedSamples);
    std::cout << "PASS: bounded overflow " << (bySamples ? "sample" : "slot") << " limit, newest dropped=" << 6-accepted << ", PTS/duration retained with delivery-side silence\n";
}
void cancellationCounters() {
    auto gate=std::make_shared<GateSink>();
    BufferedAudioSink sink(gate);
    auto first=frame(0); CHECK(sink.push(first.get())); gate->entered.get_future().wait();
    for (int i=1; i<10; ++i) { auto f=frame(i); CHECK(sink.push(f.get())); }
    CHECK(!sink.joinFor(5ms));
    sink.cancel(); CHECK(sink.joinFor(1s));
    CHECK(sink.diagnostics().downstreamDroppedSamples == 4800);
    CHECK(sink.diagnostics().downstreamDroppedFrames == 10);
    CHECK(sink.diagnostics().blockedNs == 0 && gate->finishes == 0);
    std::cout << "PASS: abort counts in-flight/ring samples once, no capture wait or late EOF\n";
}
void ringWrap() {
    FFAFrameQueue queue;
    auto delivery=std::make_shared<QueueAudioSink>(queue, true);
    int starts = 0;
    const auto sourceThread = std::this_thread::get_id();
    BufferedAudioSink sink(delivery, 2, BufferedAudioSink::kCapacitySamples, [&] {
        CHECK(std::this_thread::get_id() != sourceThread); ++starts;
    });
    for (int i=0; i<100; ++i) {
        auto f=frame(i); CHECK(sink.push(f.get()));
        auto data=queue.receive(); Frame received(data.item);
        CHECK(data.ok() && received->pts == 1000000+i*10000);
    }
    sink.finish(); CHECK(queue.receive().result == MediaQueueResult::Eof && sink.joinFor(1s));
    CHECK(sink.diagnostics().downstreamDroppedSamples == 0 && starts == 1);
    std::cout << "PASS: SPSC slot wrap/reuse retains frames and timestamps\n";
}
void cancelledLifetime() {
    FFAFrameQueue queue;
    auto delivery=std::make_shared<QueueAudioSink>(queue, false);
    auto sink=std::make_unique<BufferedAudioSink>(delivery);
    for (int i=0; i<100; ++i) { auto f=frame(i); CHECK(sink->push(f.get())); }
    const auto before=std::chrono::steady_clock::now();
    sink->cancel(); queue.start(); // old delivery must not touch the new session
    CHECK(sink->joinFor(1s));
    auto f=frame(101); CHECK(!sink->push(f.get()));
    sink->finish(); sink.reset();
    CHECK(queue.peekEmpty() && !queue.isClosed() && !queue.isFinished());
    std::cout << "PASS: cancelled delivery joins without UAF/new-session writes ms="
              << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count() << '\n';
    // Destroy the adapter before the gated task's final return. Shared State owns
    // sink and buffered AVFrames until the delivery task has exited.
    auto gate=std::make_shared<GateSink>();
    std::weak_ptr<GateSink> weak=gate;
    { BufferedAudioSink buffered(gate); auto data=frame(0); buffered.push(data.get());
      gate->entered.get_future().wait(); gate.reset(); }
    for (int i=0; i<1000 && !weak.expired(); ++i) std::this_thread::sleep_for(1ms);
    CHECK(weak.expired());
}
int main() {
    try { delayedConsumer(false); delayedConsumer(true); overflow(false); overflow(true); cancellationCounters(); ringWrap(); cancelledLifetime(); }
    catch(const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

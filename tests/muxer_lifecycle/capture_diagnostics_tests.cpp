#include "capture/test_video_start_delay.h"
#include "capture/wasapi_packet_diagnostics.h"
#include "capture/wasapi_queue_sink.h"
#include "thread/ffmuxerthread.h"
#include "queue/ffapacketqueue.h"
#include "queue/ffvpacketqueue.h"
#include <future>
#include <iostream>
#include <stdexcept>
using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
struct ItemTraits {
    static std::atomic<int> live;
    static int *allocateFromSrc(int *p) { return p; }
    static void releaseSrc(int *p) { delete p; --live; }
    static void release(int *&p) { releaseSrc(p); p=nullptr; }
};
std::atomic<int> ItemTraits::live{0};
int *item(int value) { ++ItemTraits::live; return new int(value); }
void queueCounters() {
    FFBoundedQueue<int, ItemTraits> q(1);
    CHECK(q.enqueueFromSrc(item(1)) == MediaQueueResult::Ok);
    CHECK(q.diagnostics().blockedNs == 0 && q.diagnostics().rejected == 0);
    uint64_t firstWait = 0;
    auto pushed = std::async(std::launch::async, [&] { return q.enqueueFromSrc(item(2), nullptr, &firstWait); });
    CHECK(pushed.wait_for(30ms) == std::future_status::timeout);
    auto received = q.receive(); CHECK(received.ok()); ItemTraits::release(received.item);
    CHECK(pushed.get() == MediaQueueResult::Ok);
    auto first = q.diagnostics();
    CHECK(first.blockedEnqueues == 1 && first.blockedNs == firstWait && firstWait >= 20000000);
    CHECK(first.maxBlockedNs == firstWait);
    auto cancelled = std::async(std::launch::async, [&] { return q.enqueueFromSrc(item(3)); });
    CHECK(cancelled.wait_for(40ms) == std::future_status::timeout);
    q.sendFinish(); CHECK(cancelled.get() == MediaQueueResult::Closed);
    auto second = q.diagnostics();
    CHECK(second.blockedEnqueues == 2 && second.blockedNs >= firstWait+30000000);
    CHECK(second.maxBlockedNs == std::max(firstWait, second.blockedNs-firstWait));
    CHECK(second.rejected == 1 && second.discarded == 0);
    CHECK(!q.tryEnqueueFromSrc(item(4)) && q.diagnostics().rejected == 2);
    q.close(); q.close(); CHECK(q.diagnostics().discarded == 1 && ItemTraits::live == 0);
    std::cout << "PASS: queue backpressure total_ms=" << second.blockedNs/1000000.0
              << " max_ms=" << second.maxBlockedNs/1000000.0 << " rejected=2 discarded=1\n";
    q.start(); CHECK(q.diagnostics().blockedNs == 0 && q.diagnostics().discarded == 0 && q.diagnostics().rejected == 0);
}
void restartCounters() {
    FFBoundedQueue<int, ItemTraits> q(1);
    CHECK(q.enqueueFromSrc(item(1)) == MediaQueueResult::Ok);
    std::atomic<bool> cancel{false};
    uint64_t waited = 0;
    auto pushed = std::async(std::launch::async, [&] {
        return q.enqueueFromSrc(item(2), &cancel, &waited);
    });
    CHECK(pushed.wait_for(30ms) == std::future_status::timeout);
    cancel = true; q.close(); q.start();
    CHECK(pushed.get() == MediaQueueResult::Closed && waited >= 20000000);
    CHECK(q.diagnostics().blockedNs == 0 && q.diagnostics().rejected == 0);
    CHECK(q.diagnostics().discarded == 0 && ItemTraits::live == 0);
    std::cout << "PASS: old producer retains its own wait without counting against restarted queue\n";
}
void deviceCounters() {
    capture::wasapi::PacketDiagnostics s;
    s.observe(100, 480, 48000, false, true);
    s.observe(1060, 480, 48000, true, true); // missing 480 frames
    CHECK(s.discontinuities == 1 && s.estimatedLostSamples == 480);
    s.observe(5000, 480, 48000, false, true); // unflagged idle is not loss
    s.observe(5480, 480, 48000, true, false);
    s.resetPosition(); s.observe(0, 480, 48000, true, true);
    s.observe(0, 480, 48000, true, true); // backward position cannot quantify loss
    CHECK(s.discontinuities == 4 && s.unquantifiedDiscontinuities == 3 && s.estimatedLostSamples == 480);
    s.resetPosition(); s.observe(0, 441, 44100, false, true);
    s.observe(882, 441, 44100, true, true);
    CHECK(s.discontinuities == 5 && s.estimatedLostSamples == 960);
    std::cout << "PASS: device discontinuities=5 lost_48k_estimate=960 unquantified=3\n";
}
void delayHook() {
    using capture::test::VideoStartDelay;
    CHECK(VideoStartDelay::parse(nullptr) == 0ms);
    for (const auto *v : {"", "0", "-1", "oops", "10x", "60001"}) CHECK(VideoStartDelay::parse(v) == 0ms);
    CHECK(VideoStartDelay::parse("2000") == 2s);
    int waits = 0, logs = 0;
    auto wait = [&](auto duration) { CHECK(duration == 2s); ++waits; return false; };
    auto log = [&](auto ms) { CHECK(ms == 2000); ++logs; };
    VideoStartDelay disabled(0ms); CHECK(disabled.beforeFirstPacket(wait, log)); CHECK(waits == 0 && logs == 0);
    VideoStartDelay enabled(2s); CHECK(enabled.beforeFirstPacket(wait, log)); CHECK(enabled.beforeFirstPacket(wait, log));
    CHECK(waits == 1 && logs == 1);
    VideoStartDelay cancelled(2s); CHECK(!cancelled.beforeFirstPacket([](auto) { return true; }, log));
    CHECK(logs == 2);
    std::cout << "PASS: delay disabled by default, enabled once, cancellable; no timestamp edits\n";
}
AVFrame *frame() {
    auto *f = av_frame_alloc(); CHECK(f);
    f->format = AV_SAMPLE_FMT_FLTP; f->sample_rate = 48000; f->nb_samples = 480;
    av_channel_layout_default(&f->ch_layout, 2); CHECK(av_frame_get_buffer(f, 0) == 0);
    return f;
}
void sinkCounters() {
    FFAFrameQueue queue;
    capture::wasapi::QueueAudioSink sink(queue, false);
    for (int i=0; i<30; ++i) { auto *f=frame(); CHECK(sink.push(f)); av_frame_free(&f); }
    CHECK(sink.diagnostics().blockedNs == 0);
    auto push = std::async(std::launch::async, [&] { auto *f=frame(); const auto ok=sink.push(f); av_frame_free(&f); return ok; });
    CHECK(push.wait_for(30ms) == std::future_status::timeout);
    sink.cancel(); CHECK(!push.get());
    auto stats = sink.diagnostics();
    CHECK(stats.blockedNs >= 20000000 && stats.maxBlockedNs == stats.blockedNs && stats.rejectedSamples == 480);
    auto *f=frame(); CHECK(!sink.push(f)); av_frame_free(&f);
    CHECK(sink.diagnostics().blockedNs == stats.blockedNs && sink.diagnostics().rejectedSamples == 960);
    std::cout << "PASS: delivery sink full-queue wait_ms=" << stats.blockedNs/1000000.0 << " rejected_samples=960\n";
}
void muxDropCounters() {
    FFAPacketQueue audio; FFVPacketQueue video;
    FFMuxerThread mux; mux.init(&audio, &video, nullptr, nullptr, nullptr, nullptr);
    auto *p=av_packet_alloc(); CHECK(p && av_new_packet(p, 1) == 0);
    audio.enqueue(p); // then explicitly discarded on abort
    audio.close();
    CHECK(av_new_packet(p, 1) == 0); video.enqueue(p);
    video.sendFinish(); CHECK(av_new_packet(p, 1) == 0); video.enqueue(p); // rejected after EOF
    av_packet_free(&p);
    CHECK(mux.diagnostics().droppedPackets == 2);
    auto received=video.receive(); CHECK(received.ok()); FFPacketTraits::release(received.item);
    CHECK(mux.diagnostics().droppedPackets == 2); // delivered packet isn't a drop
    mux.close();
    audio.start(); video.start(); mux.init(&audio, &video, nullptr, nullptr, nullptr, nullptr);
    CHECK(mux.diagnostics().droppedPackets == 0 && mux.diagnostics().bootstrapMaxPackets == 0);
    std::cout << "PASS: mux drop aggregation counts discarded/rejected packets, resets per session\n";
}
int main() {
    try { queueCounters(); restartCounters(); deviceCounters(); delayHook(); sinkCounters(); muxDropCounters(); }
    catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

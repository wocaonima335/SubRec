#include "capture/wasapi_queue_sink.h"
#include "capture/wasapi_worker_lifetime.h"
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace capture::wasapi;
using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__)); } while (false)
struct FrameDelete { void operator()(AVFrame *p) const { av_frame_free(&p); } };
using Frame = std::unique_ptr<AVFrame, FrameDelete>;
Frame frame() {
    Frame f(av_frame_alloc()); CHECK(f);
    f->format = AV_SAMPLE_FMT_FLTP; f->sample_rate = 48000; f->nb_samples = 480;
    av_channel_layout_default(&f->ch_layout, 2); CHECK(av_frame_get_buffer(f.get(), 0) == 0);
    CHECK(av_dict_set_int(&f->metadata, "capture_time_us", 123456, 0) == 0);
    return f;
}
int main() {
    try {
        std::weak_ptr<FFAFrameQueue::State> weakQueue;
        std::weak_ptr<QueueAudioSink> weakSink;
        WorkerLifetime worker;
        std::promise<void> entered, release;
        auto resume = release.get_future().share();
        std::atomic<bool> pushOk{true}, latePushOk{true};
        {
            auto queue = std::make_unique<FFAFrameQueue>(); queue->start();
            auto sink = std::make_shared<QueueAudioSink>(*queue, true);
            weakQueue = queue->sharedState(); weakSink = sink;
            auto sample = frame(); CHECK(sink->push(sample.get()) && sample->nb_samples == 0);
            auto first = queue->receive(); Frame received(first.item);
            CHECK(received && received->pts == 123456 && received->time_base.den == 1000000);
            for (int i = 0; i < 30; ++i) { auto f = frame(); CHECK(sink->push(f.get())); }
            worker.start([sink, &entered, resume, &pushOk, &latePushOk] {
                auto f = frame(); entered.set_value(); pushOk = sink->push(f.get());
                resume.wait(); // Fake a driver remaining blocked after queue cancellation.
                auto late = frame(); latePushOk = sink->push(late.get()); sink->finish();
            });
            entered.get_future().wait();
            const auto start = std::chrono::steady_clock::now();
            CHECK(!worker.joinFor(20ms));
            sink->cancel();
            queue->start(); // deliberately race restart against the awakened old push
            worker.abandon();
            sink->cancel(); sink->finish();
            CHECK(!queue->isClosed() && !queue->isFinished() && queue->peekEmpty());
            queue.reset(); sink.reset();
            CHECK(!weakQueue.expired() && !weakSink.expired());
            std::cout << "PASS full queue cancellation + detach ms="
                      << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count() << '\n';
        }
        release.set_value();
        auto signal = worker.completion();
        { std::unique_lock<std::mutex> lock(signal->mutex);
          CHECK(signal->cv.wait_for(lock, 2s, [&] { return signal->done; })); CHECK(!signal->error); }
        CHECK(!pushOk && !latePushOk && weakQueue.expired() && weakSink.expired());
        std::cout << "PASS cancelled producer cannot write/EOF/close a new session; sink/queue reclaimed after worker exits\n";
        // Normal EOF is still delivered without closing the queue.
        FFAFrameQueue queue;
        QueueAudioSink sink(queue, false); auto f = frame(); CHECK(sink.push(f.get())); sink.finish();
        Frame data(queue.receive().item); CHECK(data && queue.receive().result == MediaQueueResult::Eof);
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

#include "queue/recordersyncqueue.h"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

int main() {
    try {
        RecorderSyncQueue queue;
        AVChannelLayout layout = AV_CHANNEL_LAYOUT_STEREO;
        queue.configureAudio(AV_SAMPLE_FMT_FLTP, 48000, layout, 1024, {1,48000});
        queue.configureVideo({30,1}, {1,30});
        AVFrame *audio = av_frame_alloc(); CHECK(audio);
        audio->format = AV_SAMPLE_FMT_FLTP; audio->sample_rate = 48000; audio->nb_samples = 1600;
        CHECK(av_channel_layout_copy(&audio->ch_layout, &layout) == 0);
        CHECK(av_frame_get_buffer(audio, 0) == 0);
        av_samples_set_silence(audio->extended_data, 0, 1600, 2, AV_SAMPLE_FMT_FLTP);
        auto push = [&] {
            CHECK(queue.pushAudioFrame(audio) == MediaQueueResult::Ok);
            for (;;) {
                auto ready = queue.takeReadyAudioFrame();
                if (!ready.ok()) break;
                av_frame_free(&ready.item);
            }
        };
        int64_t previous = -1;
        for (int n = 0; n < 200000; ++n) {
            push();
            const auto next = queue.reserveVideoFrame();
            CHECK(!next.drop && next.pts > previous);
            // Explicitly exercise both sides of the old 50000/150000 collisions.
            if (n == 50001 || n == 150001) CHECK(next.pts == previous + 1);
            previous = next.pts;
        }
        CHECK(queue.droppedVideoFrames() == 0);
        const auto catches = queue.videoCatchupCount();
        for (int n = 0; n < 300; ++n) push(); // Audio clock advances ten seconds.
        auto jump = queue.reserveVideoFrame();
        CHECK(!jump.drop && jump.pts > previous + 290);
        CHECK(queue.videoCatchupCount() == catches + 1);
        previous = jump.pts;
        for (int n = 0; n < 1000; ++n) {
            push(); auto next = queue.reserveVideoFrame();
            CHECK(!next.drop && next.pts > previous); previous = next.pts;
        }
        av_frame_free(&audio);
        // Rescale from ticks to a different encoder time base, never via microseconds.
        queue.reset(false); queue.configureVideo({30,1}, {1,90000});
        for (int n = 0; n < 100; ++n) CHECK(queue.reserveVideoFrame().pts == n * 3000);
        queue.reset(); queue.configureVideo({30,1}, {1,30});
        for (int n = 0; n < 5; ++n) CHECK(!queue.reserveVideoFrame().drop);
        CHECK(queue.reserveVideoFrame().drop && queue.droppedVideoFrames() == 1);
        std::cout << "PASS 200000 audio/video ticks, catch-up, rescale and startup drops\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

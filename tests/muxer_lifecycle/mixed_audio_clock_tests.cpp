#include "capture/audio_timing.h"
#include "queue/recordersyncqueue.h"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

int main()
{
    try {
        RecorderSyncQueue queue;
        queue.setAudioClockDelayUs(capture::audio::mixHoldbackUs);
        AVChannelLayout layout = AV_CHANNEL_LAYOUT_STEREO;
        // Startup configures the delay before either worker configures its format.
        queue.configureAudio(AV_SAMPLE_FMT_FLTP, 48000, layout, 1024, {1,48000});
        queue.configureVideo({30,1}, {1,90000});
        AVFrame *audio = av_frame_alloc(); CHECK(audio);
        struct Cleanup { AVFrame *&frame; ~Cleanup() { av_frame_free(&frame); } } cleanup{audio};
        audio->format = AV_SAMPLE_FMT_FLTP; audio->sample_rate = 48000; audio->nb_samples = 1600;
        CHECK(av_channel_layout_copy(&audio->ch_layout, &layout) == 0);
        CHECK(av_frame_get_buffer(audio, 0) == 0);
        av_samples_set_silence(audio->extended_data, 0, 1600, 2, AV_SAMPLE_FMT_FLTP);
        int64_t nextAudioPts = 0;
        auto push = [&] {
            CHECK(queue.pushAudioFrame(audio) == MediaQueueResult::Ok);
            while (true) {
                auto ready = queue.takeReadyAudioFrame();
                if (!ready.ok()) break;
                const bool unchanged = ready.item->pts == nextAudioPts;
                nextAudioPts += 1024;
                av_frame_free(&ready.item);
                CHECK(unchanged); // Compensation never shifts or removes PCM samples.
            }
        };
        int64_t previous = -1;
        for (int n=0; n<5; ++n) {
            auto video = queue.reserveVideoFrame();
            CHECK(!video.drop && video.pts == n*3000);
            previous = video.pts;
        }
        CHECK(queue.reserveVideoFrame().drop); // Original 150 ms startup limit.
        for (int n=0; n<200000; ++n) {
            push();
            auto video = queue.reserveVideoFrame();
            CHECK(!video.drop && video.pts > previous);
            // Audio output is 200 ms behind current capture: six video ticks.
            // Existing ceil catch-up can add one tick at a rounded-us boundary.
            CHECK(video.pts >= (n+6LL)*3000 && video.pts <= (n+7LL)*3000);
            CHECK(video.audioClockUs == av_rescale_q(nextAudioPts, AVRational{1,48000}, AVRational{1,1000000}));
            previous = video.pts;
        }
        queue.onPauseChanged(true, 0);
        CHECK(queue.reserveVideoFrame().drop);
        CHECK(queue.pushAudioFrame(audio) == MediaQueueResult::Again);
        queue.onPauseChanged(false, 0);
        push();
        CHECK(queue.reserveVideoFrame().pts == previous+3000);
        for (int n=0; n<300; ++n) push();
        auto jump = queue.reserveVideoFrame();
        CHECK(!jump.drop && jump.pts > previous + 290*3000);
        push(); CHECK(queue.reserveVideoFrame().pts > jump.pts);
        // A new single-source session must not inherit the mixed-source delay.
        queue.reset(); queue.configureAudio(AV_SAMPLE_FMT_FLTP, 48000, layout, 1024, {1,48000});
        queue.configureVideo({30,1}, {1,30}); nextAudioPts = 0; push();
        CHECK(queue.reserveVideoFrame().pts == 0);
        queue.reset(false); queue.setAudioClockDelayUs(capture::audio::mixHoldbackUs);
        queue.configureVideo({30,1}, {1,30});
        CHECK(queue.reserveVideoFrame().pts == 0 && queue.reserveVideoFrame().pts == 1);
        std::cout << "PASS mixed clock delay: 200000 monotonic ticks, PCM unchanged, pause, jump, reset\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

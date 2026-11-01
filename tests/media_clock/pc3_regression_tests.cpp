#include "queue/video_tick_scheduler.h"
#include <climits>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
extern "C" {
#include <libavutil/mem.h>
}
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__)); } while(false)

void jitter() {
    VideoTickScheduler old, paced;
    CHECK(old.configure() && paced.configure());
    // PC3: 27003 received = 23581 accepted + 3422 collisions, 3423 gaps.
    // Receive stamps straddling the nearest-rounding boundary by only 2 ms
    // reproduce those counters exactly. These are synthetic stamps, not an
    // invented per-frame trace: PC3 logged captureUs only every 300 frames.
    for (int64_t n=0; n<27003; ++n) {
        const bool late = n >= 1 && n <= 6843 && n % 2;
        const int64_t us = av_rescale_q(n, AVRational{1,30}, AVRational{1,1000000})
                           + 49000 + (late ? 2000 : 0);
        old.decide(us);
        const auto d = paced.decidePaced(us);
        CHECK(d.accepted() && d.tick == n+1);
    }
    CHECK(old.counters().slotCollisions == 3422 && old.counters().frames == 23581);
    CHECK(old.counters().laggedTicks == 3423);
    CHECK(paced.counters().frames == 27003 && paced.counters().slotCollisions == 0);
    CHECK(paced.counters().laggedTicks == 1); // Preserved startup offset only.
    for (AVRational fps : {AVRational{30,1}, AVRational{30000,1001}}) {
        paced.configure(fps);
        for (int64_t n=0; n<108000; ++n) {
            // Windows receive jitter near the half-frame boundary, 0..30 ms;
            // no integration of rounded periods, including at fractional fps.
            const auto nominal = av_rescale_q(n, av_inv_q(fps), AVRational{1,1000000});
            const auto d = paced.decidePaced(nominal + (n % 2 ? 30000 : 1000));
            CHECK(d.accepted() && d.tick == n);
        }
        const auto count = paced.counters().frames;
        CHECK(!paced.decidePaced(0).accepted()); // stale/duplicate timestamps
        CHECK(paced.counters().frames == count);
        const auto d = paced.decidePaced(4000000000LL);
        CHECK(d.accepted() && d.skippedTicks > 0); // real outage is not compressed
        CHECK(!paced.decidePaced(4000000000LL).accepted());
        CHECK(paced.decidePaced(-1).kind == TickDecision::Kind::InvalidTime);
    }
    paced.configure(); CHECK(paced.decidePaced(400000).tick == 12); // delayed startup
    paced.setHoldFrameCallback([](int64_t, int64_t) { return true; });
    CHECK(paced.requestHoldover(500000, 10) == 3);
    CHECK(!paced.decidePaced(410000).accepted()); // stale capture after hold
    CHECK(paced.decidePaced(533333).tick == 16);
    paced.configure({INT_MAX,1});
    CHECK(paced.decidePaced(INT64_MAX).kind == TickDecision::Kind::InvalidTime);
}

void pauses() {
    MediaClock clock; clock.start(1000000);
    VideoTickScheduler video, legacy;
    CHECK(video.configure() && legacy.configure());
    int64_t paused = 0;
    // One captured frame just before each 25 s boundary is missing; the first
    // resumed capture is 40 ms after it. The old last+1 anchor hides that time.
    // Audio sample timestamps use the same pause mapping at 48 kHz.
    for (int cycle=0; cycle<=10; ++cycle) {
        const int64_t begin = cycle*25000000LL;
        for (int n=0; n<749; ++n) {
            const int64_t media = begin + 40000 + av_rescale_q(n, AVRational{1,30}, AVRational{1,1000000});
            const auto wall = 1000000 + paused + media;
            CHECK(clock.mediaUs(wall) == media);
            const bool resumed = cycle > 0 && n == 0;
            if (resumed) CHECK(legacy.beginSegment(media));
            const auto old = legacy.decide(media);
            const auto d = video.decidePaced(media, resumed);
            CHECK(d.accepted());
            const auto audioSample = av_rescale_q(clock.mediaUs(wall), AVRational{1,1000000}, AVRational{1,48000});
            const auto audioUs = av_rescale_q(audioSample, AVRational{1,48000}, AVRational{1,1000000});
            const auto videoUs = av_rescale_q(d.tick, AVRational{1,30}, AVRational{1,1000000});
            CHECK(std::abs((audioUs-videoUs) - 6667) <= 1); // constant across all cycles
            if (n==0) CHECK(d.tick - old.tick == cycle); // old accumulated 33.333 ms/resume
        }
        const auto boundary = 1000000 + paused + begin + 25000000;
        clock.pause(boundary); clock.resume(boundary + 5000000);
        paused += 5000000;
    }
    CHECK(video.counters().slotCollisions == 0);
}

void stamps() {
    // Exercise real FFmpeg ownership with a callback on each CaptureStamp.
    // Includes restamping, props copies, clones, queue move and reused hold shell.
    int live = 0;
    auto release = [](void *opaque, uint8_t *data) { --*static_cast<int*>(opaque); av_free(data); };
    AVFrame *source=av_frame_alloc(), *props=av_frame_alloc(), *queue=av_frame_alloc(), *held=av_frame_alloc();
    CHECK(source && props && queue && held);
    source->format=AV_PIX_FMT_YUV420P; source->width=source->height=16;
    CHECK(av_frame_get_buffer(source,32)==0);
    for (int n=0;n<27003;++n) {
        auto *data=static_cast<uint8_t*>(av_malloc(sizeof(media_clock_detail::CaptureStamp)));
        CHECK(data);
        const media_clock_detail::CaptureStamp stamp{media_clock_detail::captureStampTag,1,0,n};
        std::memcpy(data,&stamp,sizeof(stamp));
        av_buffer_unref(&source->opaque_ref);
        source->opaque_ref=av_buffer_create(data,sizeof(stamp),release,&live,0); CHECK(source->opaque_ref); ++live;
        CHECK(av_frame_copy_props(props,source)==0);
        AVFrame *clone=av_frame_clone(source); CHECK(clone);
        av_frame_move_ref(queue,clone); av_frame_free(&clone);
        av_frame_unref(held); CHECK(av_frame_ref(held,queue)==0);
        av_frame_unref(queue); av_frame_unref(props);
        CHECK(setFrameCaptureUs(source,n+1)==0); // held must retain the old stamp
        int64_t us=-1; CHECK(frameCaptureUs(held,&us) && us==n);
        CHECK(live==1); av_frame_unref(held); CHECK(live==0);
    }
    av_frame_free(&source); av_frame_free(&props); av_frame_free(&queue); av_frame_free(&held);
    CHECK(live==0);
}
int main(int argc,char **argv) {
    try { const std::string mode=argc>1?argv[1]:"jitter";
        if(mode=="jitter") jitter(); else if(mode=="pauses") pauses(); else stamps();
        std::cout<<"PASS pc3 "<<mode<<'\n';
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n';return 1; }
}

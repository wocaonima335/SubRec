#include "queue/video_tick_scheduler.h"
#include <atomic>
#include <climits>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <string>
#include <vector>
extern "C" {
#include <libavutil/time.h>
}
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

void clockTest() {
    MediaClock clock;
    CHECK(clock.mediaUs(50) == AV_NOPTS_VALUE && clock.nowMediaUs() == AV_NOPTS_VALUE);
    clock.start(1000); CHECK(clock.mediaUs(999) == 0 && clock.mediaUs(1050) == 50);
    clock.pause(1100); clock.pause(1150);
    CHECK(clock.isPaused() && clock.mediaUs(1200) == 100 && clock.pausedTotalUs() == 0);
    clock.resume(1090); CHECK(clock.isPaused());
    clock.resume(1400); clock.resume(1450);
    CHECK(!clock.isPaused() && clock.pausedTotalUs() == 300 && clock.mediaUs(1500) == 200);
    CHECK(clock.mediaUs(1050) == 50 && clock.mediaUs(1200) == 100); // Historical captures.
    clock.pause(1300); CHECK(!clock.isPaused());
    clock.pause(1600); clock.resume(1800);
    CHECK(clock.mediaUs(1900) == 400 && clock.pausedTotalUs() == 500);
    std::atomic<bool> failed{false};
    std::thread reader([&] { for (int i=0;i<10000;++i) if (clock.mediaUs(1050) != 50) failed = true; });
    for (int64_t t=2000;t<12000;t+=10) { clock.pause(t); clock.resume(t+5); }
    reader.join(); CHECK(!failed);
    const auto now = av_gettime_relative(); clock.start(now);
    CHECK(!clock.isPaused() && clock.pausedTotalUs() == 0 && clock.nowMediaUs() >= 0);
    clock.pause(now+1); CHECK(clock.mediaUs(AV_NOPTS_VALUE) == AV_NOPTS_VALUE);
}

void tickTest(AVRational fps) {
    VideoTickScheduler ticks; CHECK(ticks.decide(0).kind == TickDecision::Kind::NotConfigured);
    CHECK(!ticks.configure({0,1}) && ticks.configure(fps));
    int64_t previous = -1;
    for (int64_t n=0;n<1000000;++n) {
        const int64_t us = av_rescale_q(n, av_inv_q(fps), AVRational{1,1000000});
        const auto d = ticks.decide(us);
        CHECK(d.accepted() && d.tick == n && d.tick > previous && d.skippedTicks == 0);
        // Independent integer error bound against the exact input-time rational.
        const int64_t error = d.tick*int64_t(fps.den)*1000000-us*fps.num;
        CHECK(std::abs(error)*2 <= int64_t(fps.den)*1000000);
        if (n == 50000 || n == 150000) CHECK(d.tick == previous+1);
        previous = d.tick;
    }
    CHECK(ticks.counters().frames == 1000000 && ticks.counters().laggedTicks == 0);
    CHECK(!ticks.configure({-1,1}) && ticks.lastTick() == previous);
    ticks.reset(); uint64_t state=17, accepted=0, collisions=0, skipped=0; previous=-1;
    int64_t us=0;
    for (int n=0;n<100000;++n) {
        state = state*6364136223846793005ULL+1;
        us = std::max<int64_t>(0,us+int64_t((state>>32)%140000)-40000);
        const auto d=ticks.decide(us);
        if (d.accepted()) { CHECK(d.tick>previous); previous=d.tick; ++accepted; skipped+=d.skippedTicks; }
        else { CHECK(d.kind==TickDecision::Kind::SlotCollision); ++collisions; }
    }
    const auto c=ticks.counters(); CHECK(c.frames==accepted && c.slotCollisions==collisions && c.laggedTicks==skipped);
    CHECK(ticks.decide(-1).kind==TickDecision::Kind::InvalidTime && ticks.lastTick()==previous);
    CHECK(ticks.decide(AV_NOPTS_VALUE).kind==TickDecision::Kind::InvalidTime);
    ticks.configure({INT_MAX,1}); CHECK(ticks.decide(INT64_MAX).kind==TickDecision::Kind::InvalidTime);
}
void segmentTest() {
    MediaClock clock; clock.start(1000000);
    VideoTickScheduler ticks; CHECK(ticks.configure());
    CHECK(ticks.decide(clock.mediaUs(1100000)).tick==3); // Preserve positive initial offset.
    clock.pause(1200000); clock.resume(2200000);
    CHECK(ticks.beginSegment(clock.mediaUs(2250000)));
    CHECK(ticks.decide(clock.mediaUs(2250000)).tick==4);
    CHECK(ticks.decide(clock.mediaUs(2283333)).tick==5);
    CHECK(!ticks.beginSegment(-1));
    CHECK(ticks.decide(0).kind==TickDecision::Kind::InvalidTime);
    ticks.reset(); CHECK(ticks.decide(0).tick==0);
    bool succeed=false; std::vector<int64_t> held;
    ticks.setHoldFrameCallback([&](int64_t tick,int64_t mediaUs) {
        CHECK(mediaUs==av_rescale_q(tick, AVRational{1,30}, AVRational{1,1000000}));
        if (succeed) held.push_back(tick); return succeed;
    });
    CHECK(ticks.requestHoldover(200000,10)==0 && ticks.lastTick()==0);
    succeed=true;
    CHECK(ticks.requestHoldover(200000,2)==2 && ticks.lastTick()==2);
    CHECK(ticks.requestHoldover(200000,10)==4 && ticks.lastTick()==6);
    CHECK(ticks.requestHoldover(200001,10)==0 && ticks.counters().holdoverTicks==6);
    CHECK(ticks.decide(200000).kind==TickDecision::Kind::SlotCollision);
    CHECK(ticks.decide(266667).skippedTicks==1); // Normal gaps do not invoke hold callback.
    CHECK(held.size()==6 && ticks.counters().frames==2);
    ticks.reset(); CHECK(ticks.requestHoldover(1000000)==0); // No last frame.
}
int main(int argc,char **argv) {
    try {
        const std::string mode=argc>1?argv[1]:"clock";
        if(mode=="clock") clockTest(); else if(mode=="30") tickTest({30,1});
        else if(mode=="ntsc") tickTest({30000,1001}); else segmentTest();
        std::cout<<"PASS "<<mode<<'\n';
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n';return 1; }
}

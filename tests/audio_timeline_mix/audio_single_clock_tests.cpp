#include "filter/audio_timeline_mixer.h"
#include "queue/media_clock.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__)); } while(false)

AudioTimelineMixer::Frame input(int64_t us, int samples, int rate=48000) {
    AudioTimelineMixer::Frame f(av_frame_alloc());CHECK(f);
    f->format=AV_SAMPLE_FMT_FLTP;f->sample_rate=rate;f->nb_samples=samples;
    f->pts=us;f->time_base={1,1000000};av_channel_layout_default(&f->ch_layout,2);
    CHECK(av_frame_get_buffer(f.get(),0)==0);
    for(int ch=0;ch<2;++ch) for(int i=0;i<samples;++i) reinterpret_cast<float*>(f->extended_data[ch])[i]=.2f;
    return f;
}
void single(unsigned source) {
    MediaClock clock;clock.start(1000000);AudioTimelineMixer mixer(clock);
    // Single system or microphone source, with an initial 100 ms gap and a
    // missing second half. Inactive source never blocks clock-driven silence.
    auto f=input(1100000,19200);mixer.push(source,*f);
    mixer.finish(source);mixer.finish(1-source);
    int64_t samples=0;double signal=0;
    while(auto out=mixer.renderAt(2000000)) {
        CHECK(out->pts==samples);samples+=out->nb_samples;
        for(int i=0;i<out->nb_samples;++i) {
            const auto n=out->pts+i; const float value=reinterpret_cast<float*>(out->extended_data[0])[i];
            if(n<4800 || n>24100) CHECK(value==0);else signal+=std::abs(value);
        }
    }
    CHECK(samples==48000 && mixer.position()==48000 && signal>3000);
    CHECK(mixer.stats().silence[1-source]==48000 && mixer.stats().frames[source]>0);
}
void pauses() {
    MediaClock clock;clock.start(1000000);AudioTimelineMixer mixer(clock);
    // Enqueue BEFORE the pause is known. Mapping must wait for the render cutoff.
    auto f=input(1000000,48000);
    for (int ch=0;ch<2;++ch) for (int i=9600;i<28800;++i)
        reinterpret_cast<float*>(f->extended_data[ch])[i]=.6f; // Must be entirely removed.
    mixer.push(0,*f);mixer.finish(0);mixer.finish(1);
    clock.pause(1200000);clock.resume(1600000);
    int64_t samples=0;
    while(auto out=mixer.renderAt(2000000)) {
        CHECK(out->pts==samples);samples+=out->nb_samples;
        for (int i=0;i<out->nb_samples;++i) CHECK(std::abs(reinterpret_cast<float*>(out->extended_data[0])[i])<.3f);
    }
    CHECK(samples==28800); // 1 second minus 400 ms pause.
    CHECK(mixer.pendingFrames(0)==0);
    // Pause lasting through EOF cannot append paused samples.
    MediaClock open;open.start(1000000);AudioTimelineMixer tail(open);
    auto g=input(1000000,48000);tail.push(1,*g);open.pause(1500000);tail.finish(0);tail.finish(1);
    samples=0;while(auto out=tail.renderAt(2000000))samples+=out->nb_samples;
    CHECK(samples==24000);
    // 44.1k input split by render cutoffs; no extra samples at final drain.
    MediaClock other;other.start(1000000);AudioTimelineMixer ntsc(other);
    auto h=input(1000000,44100,44100);ntsc.push(0,*h);ntsc.finish(0);ntsc.finish(1);
    samples=0;
    for(int64_t cutoff=1005000;cutoff<=2000000;cutoff+=5000)
        while(auto out=ntsc.renderAt(cutoff)){CHECK(out->pts==samples);samples+=out->nb_samples;}
    CHECK(samples==48000 && ntsc.pendingFrames(0)==0);
}
// Match production: capture is ahead by the holdback, but renderAt maps
// only through each cutoff. A continuously queued signal must never be
// replaced by silence while swr still owns its filter tail.
void streamingTail(int rate, unsigned source) {
    MediaClock clock; clock.start(1000000); AudioTimelineMixer mixer(clock);
    mixer.finish(1-source);
    constexpr int seconds = 10;
    int64_t nextSample = 0, samples = 0;
    auto drain = [&](int64_t cutoff) {
        while (auto out = mixer.renderAt(cutoff)) {
            CHECK(out->pts == samples); samples += out->nb_samples;
            for (int i=0;i<out->nb_samples;++i)
                CHECK(std::abs(reinterpret_cast<float*>(out->extended_data[0])[i]-.2f)<.001f);
        }
    };
    // Vary the cutoff and split 50-ms device packets at non-sample boundaries.
    for (int64_t cutoff=1005001; cutoff<1000000+seconds*1000000; cutoff+=7133) {
        while (nextSample < seconds*rate &&
               1000000+nextSample*1000000/rate < cutoff+200000) {
            auto f=input(1000000+nextSample*1000000/rate, rate/20, rate);
            mixer.push(source,*f); nextSample+=rate/20;
        }
        drain(cutoff);
    }
    mixer.finish(source); drain(1000000+seconds*1000000);
    std::cout << "streaming rate=" << rate << " samples=" << samples << " trimmed=" << mixer.stats().trimmed[source] << " silence=" << mixer.stats().silence[source] << " pending=" << mixer.pendingFrames(source) << " end=" << mixer.captureEndUs() << "\n";
    CHECK(samples == seconds*48000);
    CHECK(mixer.stats().trimmed[source] == 0);
    CHECK(mixer.stats().silence[source] == 0);
    CHECK(mixer.stats().resyncs[source] == 0);
}
int main(){try{streamingTail(48000,1);streamingTail(44100,0);single(0);single(1);pauses();std::cout<<"PASS single-source silence, sample timeline and queued pause boundaries\n";}
catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

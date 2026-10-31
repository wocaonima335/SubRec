#include "queue/recordersyncqueue.h"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
int main() {
    try {
        RecorderSyncQueue queue; AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
        queue.configureAudio(AV_SAMPLE_FMT_FLTP,48000,stereo,1024,{1,48000});
        AVFrame *frame=av_frame_alloc(); CHECK(frame);
        struct Cleanup { AVFrame *&p; ~Cleanup(){av_frame_free(&p);} } cleanup{frame};
        frame->format=AV_SAMPLE_FMT_FLTP; frame->sample_rate=48000; frame->nb_samples=480;
        CHECK(av_channel_layout_copy(&frame->ch_layout,&stereo)==0 && av_frame_get_buffer(frame,0)==0);
        av_samples_set_silence(frame->extended_data,0,480,2,AV_SAMPLE_FMT_FLTP);
        frame->sample_rate=44100; CHECK(queue.pushAudioFrame(frame)==MediaQueueResult::Error); frame->sample_rate=48000;
        frame->format=AV_SAMPLE_FMT_FLT; CHECK(queue.pushAudioFrame(frame)==MediaQueueResult::Error); frame->format=AV_SAMPLE_FMT_FLTP;
        frame->ch_layout.u.mask=AV_CH_LAYOUT_STEREO_DOWNMIX;
        CHECK(queue.pushAudioFrame(frame)==MediaQueueResult::Error); frame->ch_layout.u.mask=AV_CH_LAYOUT_STEREO;
        auto *plane=frame->extended_data[1];frame->extended_data[1]=nullptr;
        CHECK(queue.pushAudioFrame(frame)==MediaQueueResult::Error);frame->extended_data[1]=plane;
        CHECK(queue.audioClockUs()==AV_NOPTS_VALUE);
        int64_t samples=0;
        for (int i=0;i<100;++i) {
            CHECK(queue.pushAudioFrame(frame)==MediaQueueResult::Ok);
            while (true) { auto r=queue.takeReadyAudioFrame();if(!r.ok())break;
                CHECK(r.item->pts==samples && r.item->nb_samples==1024);samples+=1024;av_frame_free(&r.item); }
        }
        queue.observeVideoTick(30,{30,1});
        CHECK(queue.avDeltaUs()==queue.audioClockUs()-1000000);
        CHECK(queue.finishAudio()==MediaQueueResult::Eof);
        auto tail=queue.takeReadyAudioFrame();CHECK(tail.ok() && tail.item->pts==samples);av_frame_free(&tail.item);
        CHECK(queue.pushAudioFrame(frame)==MediaQueueResult::Error);
        queue.reset(false);queue.observeVideoTick(900,{30,1});CHECK(queue.avDeltaUs()==AV_NOPTS_VALUE);
        std::cout<<"PASS audio validation, exact chunk PTS, diagnostic-only A/V monitoring\n";
    } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}

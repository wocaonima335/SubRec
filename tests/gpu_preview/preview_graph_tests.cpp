#include "capture/gpu_preview.h"
#include <cstdio>
#include <stdexcept>
extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
}
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
void run(int w, int h, int expectedW, int expectedH) {
    AVFilterGraph *graph = avfilter_graph_alloc(); CHECK(graph); graph->nb_threads = 1;
    struct Guard { AVFilterGraph *g; ~Guard() { avfilter_graph_free(&g); } } guard{graph};
    AVFilterContext *in=nullptr, *out=nullptr, *main=nullptr;
    const auto args = "video_size="+std::to_string(w)+"x"+std::to_string(h)
        +":pix_fmt="+std::to_string(AV_PIX_FMT_BGRA)+":time_base=1/30:pixel_aspect=1/1";
    CHECK(avfilter_graph_create_filter(&in,avfilter_get_by_name("buffer"),"in",args.c_str(),nullptr,graph)==0);
    CHECK(avfilter_graph_create_filter(&out,avfilter_get_by_name("buffersink"),"out",nullptr,nullptr,graph)==0);
    CHECK(avfilter_graph_create_filter(&main,avfilter_get_by_name("buffersink"),"main",nullptr,nullptr,graph)==0);
    AVFilterInOut *inputs=avfilter_inout_alloc(), *outputs=avfilter_inout_alloc();
    CHECK(inputs && outputs);
    inputs->name=av_strdup("out"); inputs->filter_ctx=out;
    inputs->next=avfilter_inout_alloc(); CHECK(inputs->next);
    inputs->next->name=av_strdup("main"); inputs->next->filter_ctx=main;
    outputs->name=av_strdup("in"); outputs->filter_ctx=in;
    // Same split topology, with a software source instead of ddagrab. Verify the main
    // branch is unchanged and the preview converter's real scale/fps/pixel negotiation.
    const std::string spec="[in]split=2[main][p];[p]"+gpuPreviewFilterSpec(false)+"[out]";
    int ret=avfilter_graph_parse_ptr(graph,spec.c_str(),&inputs,&outputs,nullptr);
    avfilter_inout_free(&inputs); avfilter_inout_free(&outputs); CHECK(ret==0);
    CHECK(avfilter_graph_config(graph,nullptr)==0);
    AVFrame *f=av_frame_alloc(), *p=av_frame_alloc(); CHECK(f && p);
    struct Frames { AVFrame *a,*b; ~Frames(){av_frame_free(&a);av_frame_free(&b);} } frames{f,p};
    int count=0;
    for(int n=0;n<90;++n) {
        f->width=w;f->height=h;f->format=AV_PIX_FMT_BGRA;f->pts=n;
        CHECK(av_frame_get_buffer(f,0)==0);
        CHECK(av_buffersrc_add_frame_flags(in,f,0)==0);
        CHECK(av_buffersink_get_frame(main,f)==0);
        CHECK(f->width==w && f->height==h && f->format==AV_PIX_FMT_BGRA);
        av_frame_unref(f);
        while((ret=av_buffersink_get_frame(out,p))>=0) {
            CHECK(p->width==expectedW && p->height==expectedH && p->format==AV_PIX_FMT_YUV420P);
            CHECK(((p->width|p->height)&1)==0);++count;av_frame_unref(p);
        }
        CHECK(ret==AVERROR(EAGAIN));
    }
    CHECK(count>=22 && count<=24);
    std::printf("PASS preview graph %dx%d -> %dx%d frames=%d (~8 fps), main unchanged\n",w,h,expectedW,expectedH,count);
}
// Exercise the exact non-blocking capture-side topology, without a native device.
void handoff() {
    AVFilterGraph *graph = avfilter_graph_alloc(); CHECK(graph); graph->nb_threads = 1;
    struct Guard { AVFilterGraph *g; ~Guard(){ avfilter_graph_free(&g); } } guard{graph};
    AVFilterContext *source=nullptr, *split=nullptr, *main=nullptr, *preview=nullptr;
    CHECK(avfilter_graph_create_filter(&source,avfilter_get_by_name("buffer"),"in",
        "video_size=16x16:pix_fmt=bgra:time_base=1/30:pixel_aspect=1/1",nullptr,graph)==0);
    CHECK(avfilter_graph_create_filter(&split,avfilter_get_by_name("split"),"split","2",nullptr,graph)==0);
    CHECK(avfilter_graph_create_filter(&main,avfilter_get_by_name("buffersink"),"main",nullptr,nullptr,graph)==0);
    CHECK(avfilter_graph_create_filter(&preview,avfilter_get_by_name("buffersink"),"preview",nullptr,nullptr,graph)==0);
    CHECK(avfilter_link(source,0,split,0)==0);
    CHECK(avfilter_link(split,0,main,0)==0); CHECK(avfilter_link(split,1,preview,0)==0);
    CHECK(avfilter_graph_config(graph,nullptr)==0);
    AVFrame *frame=av_frame_alloc(); CHECK(frame);
    struct Frame { AVFrame *f; ~Frame(){ av_frame_free(&f); } } frameGuard{frame};
    for (int n=0;n<10000;++n) {
        frame->format=AV_PIX_FMT_BGRA; frame->width=frame->height=16; frame->pts=n;
        CHECK(av_frame_get_buffer(frame,0)==0);
        CHECK(av_buffersrc_add_frame_flags(source,frame,0)==0);
        CHECK(av_buffersink_get_frame(main,frame)==0); CHECK(frame->pts==n);
        av_frame_unref(frame);
        CHECK(av_buffersink_get_frame_flags(preview,frame,AV_BUFFERSINK_FLAG_NO_REQUEST)==0);
        CHECK(frame->pts==n); av_frame_unref(frame);
        // No preview pull drives the source or leaves a second main frame queued.
        CHECK(av_buffersink_get_frame_flags(preview,frame,AV_BUFFERSINK_FLAG_NO_REQUEST)==AVERROR(EAGAIN));
        CHECK(av_buffersrc_get_nb_failed_requests(source)==0);
        CHECK(av_buffersink_get_frame_flags(main,frame,AV_BUFFERSINK_FLAG_NO_REQUEST)==AVERROR(EAGAIN));
    }
    std::puts("PASS split handoff: 10000 frames, NO_REQUEST drains exactly one, no source requests/backlog");
}
int main() {
    try { handoff();run(1920,1080,640,360);run(1080,1920,202,360);run(320,240,320,240); }
    catch(const std::exception &e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}

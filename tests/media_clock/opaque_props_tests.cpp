#include "queue/media_clock.h"

#include <cstdio>
#include <stdexcept>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

int main()
{
    try {
        AVFilterGraph *graph = avfilter_graph_alloc();
        CHECK(graph);
        struct GraphGuard { AVFilterGraph *value; ~GraphGuard() { avfilter_graph_free(&value); } } guard{graph};

        AVFilterContext *source = nullptr;
        AVFilterContext *sink = nullptr;
        if (!avfilter_get_by_name("buffer") || !avfilter_get_by_name("scale") ||
            !avfilter_get_by_name("format") || !avfilter_get_by_name("buffersink")) {
            std::puts("SKIP software filter graph: test FFmpeg lacks buffer/scale/format filters");
            return 77;
        }
        const char *args = "video_size=16x16:pix_fmt=0:time_base=1/30:pixel_aspect=1/1";
        CHECK(avfilter_graph_create_filter(&source, avfilter_get_by_name("buffer"), "in",
                                           args, nullptr, graph) == 0);
        CHECK(avfilter_graph_create_filter(&sink, avfilter_get_by_name("buffersink"), "out",
                                           nullptr, nullptr, graph) == 0);

        AVFilterContext *scale = nullptr;
        AVFilterContext *format = nullptr;
        CHECK(avfilter_graph_create_filter(&scale, avfilter_get_by_name("scale"), "scale",
                                           "16:16", nullptr, graph) == 0);
        CHECK(avfilter_graph_create_filter(&format, avfilter_get_by_name("format"), "format",
                                           "pix_fmts=yuv420p", nullptr, graph) == 0);
        CHECK(avfilter_link(source, 0, scale, 0) == 0);
        CHECK(avfilter_link(scale, 0, format, 0) == 0);
        CHECK(avfilter_link(format, 0, sink, 0) == 0);
        CHECK(avfilter_graph_config(graph, nullptr) == 0);

        AVFrame *input = av_frame_alloc();
        AVFrame *output = av_frame_alloc();
        CHECK(input && output);
        struct FrameGuard {
            AVFrame *input;
            AVFrame *output;
            ~FrameGuard() { av_frame_free(&input); av_frame_free(&output); }
        } frames{input, output};
        input->format = AV_PIX_FMT_YUV420P;
        input->width = input->height = 16;
        input->pts = 7;
        CHECK(av_frame_get_buffer(input, 32) == 0);
        CHECK(setFrameCaptureUs(input, 123456789) == 0);
        CHECK(av_buffersrc_add_frame_flags(source, input, 0) == 0);
        CHECK(av_buffersink_get_frame(sink, output) == 0);
        int64_t captureUs = 0;
        CHECK(frameCaptureUs(output, &captureUs));
        CHECK(captureUs == 123456789);
        CHECK(output->pts == 7);
        std::puts("PASS software filter graph preserves capture stamp");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}

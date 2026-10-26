#include "muxer/ffmuxer.h"
#include <QDir>
#include <QUuid>
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <cerrno>

using namespace std::chrono_literals;
#define CHECK(test) do { if (!(test)) throw std::runtime_error(#test); } while (false)

static AVCodecContext *audio()
{
    auto *ctx = avcodec_alloc_context3(avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE));
    CHECK(ctx && ctx->codec);
    ctx->sample_rate = 48000;
    ctx->sample_fmt = AV_SAMPLE_FMT_S16;
    ctx->time_base = AVRational{1, 48000};
    av_channel_layout_default(&ctx->ch_layout, 2);
    return ctx;
}
static AVCodecContext *video()
{
    auto *ctx = avcodec_alloc_context3(avcodec_find_encoder(AV_CODEC_ID_RAWVIDEO));
    CHECK(ctx && ctx->codec);
    ctx->width = ctx->height = 16;
    ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx->time_base = AVRational{1, 30};
    ctx->framerate = AVRational{30, 1};
    return ctx;
}
static void streams(FFMuxer &mux)
{
    auto *a = audio(); auto *v = video();
    CHECK(mux.addStream(a) == 0);
    CHECK(mux.addStream(v) == 0);
    // Deliberately poison values before freeing: mux must have taken snapshots.
    a->time_base = v->time_base = AVRational{0, 0};
    avcodec_free_context(&a); avcodec_free_context(&v);
}
static void emptyAndPreconditions()
{
    { FFMuxer empty; }
    FFMuxer mux;
    CHECK(mux.writeHeader(0ms) == AVERROR(EINVAL));
    CHECK(mux.writeTrailer() == AVERROR(EINVAL));
    CHECK(mux.mux(nullptr) == AVERROR(EINVAL));
    CHECK(mux.close() == 0); CHECK(mux.close() == 0);
    CHECK(mux.init("unused", "null") == 0);
    CHECK(mux.writeTrailer() == AVERROR(EINVAL));
    streams(mux);
    AVPacket packet{};
    CHECK(mux.mux(&packet) == AVERROR(EINVAL));
    CHECK(mux.writeHeader() == 0); CHECK(mux.writeHeader() == 0);
    CHECK(mux.writeTrailer() == 0); CHECK(mux.writeTrailer() == 0);
    CHECK(mux.mux(&packet) == AVERROR_EOF);
}
static void waits()
{
    FFMuxer timeout;
    CHECK(timeout.init("unused", "null") == 0);
    auto *a = audio(); CHECK(timeout.addStream(a) == 0); avcodec_free_context(&a);
    auto start = std::chrono::steady_clock::now();
    CHECK(timeout.writeHeader(40ms) == AVERROR(ETIMEDOUT));
    CHECK(std::chrono::steady_clock::now() - start >= 30ms);
    CHECK(std::chrono::steady_clock::now() - start < 2s);
    CHECK(timeout.writeTrailer() == AVERROR(ETIMEDOUT));
    for (int mode = 0; mode < 3; ++mode) {
        FFMuxer mux;
        CHECK(mux.init("unused", "null") == 0);
        std::promise<void> entered;
        auto ready = entered.get_future();
        auto waiting = std::async(std::launch::async, [&] {
            entered.set_value(); return mux.writeHeader(3s);
        });
        ready.wait();
        CHECK(waiting.wait_for(30ms) == std::future_status::timeout);
        if (mode == 0) mux.requestAbort();
        else if (mode == 1) CHECK(mux.close() == 0);
        else streams(mux);
        CHECK(waiting.wait_for(1s) == std::future_status::ready);
        CHECK(waiting.get() == (mode == 2 ? 0 : AVERROR_EXIT));
    }
}
static void failures(const QString &root)
{
    FFMuxer mux;
    const int invalid = mux.init("unused", "definitely_not_a_muxer");
    CHECK(invalid < 0); CHECK(mux.writeHeader(0ms) == invalid);
    CHECK(mux.close() == 0);
    const int ioError = mux.init((root + "/missing/out.mp4").toStdString());
    CHECK(ioError < 0); CHECK(mux.writeHeader(0ms) == ioError);
    CHECK(mux.close() == 0);
    CHECK(mux.init("unused", "null") == 0);
    auto waiting = std::async(std::launch::async, [&] { return mux.writeHeader(3s); });
    CHECK(waiting.wait_for(30ms) == std::future_status::timeout);
    CHECK(mux.addStream(nullptr) == AVERROR(EINVAL));
    CHECK(waiting.wait_for(1s) == std::future_status::ready);
    CHECK(waiting.get() == AVERROR(EINVAL));
    CHECK(mux.close() == 0);
    // PCM/rawvideo are deliberately unsupported by MP4: header must fail terminally.
    CHECK(mux.init((root + "/bad-header.mp4").toStdString()) == 0);
    streams(mux);
    const int header = mux.writeHeader(); CHECK(header < 0);
    CHECK(mux.writeTrailer() == header);
    AVPacket packet{}; CHECK(mux.mux(&packet) == header);
}
static void freedContextsAndCloseRace(const QString &root)
{
    const auto output = (root + "/copied.nut").toStdString();
    {
        FFMuxer mux;
        CHECK(mux.init(output, "nut") == 0);
        streams(mux);
        AVRational tb{}, fps{};
        CHECK(mux.getStreamTiming(AVMEDIA_TYPE_AUDIO, &tb) == 0);
        CHECK(tb.num == 1 && tb.den == 48000);
        CHECK(mux.getStreamTiming(AVMEDIA_TYPE_VIDEO, &tb, &fps) == 0);
        CHECK(tb.num == 1 && tb.den == 30 && fps.num == 30 && fps.den == 1);
        CHECK(mux.writeHeader() == 0);
        AVPacket *pkt = av_packet_alloc(); CHECK(pkt);
        CHECK(av_new_packet(pkt, 384) == 0);
        std::memset(pkt->data, 0, pkt->size);
        pkt->stream_index = mux.getVStreamIndex();
        pkt->pts = pkt->dts = 30; pkt->duration = 1; pkt->flags = AV_PKT_FLAG_KEY;
        CHECK(mux.mux(pkt) == 0); av_packet_free(&pkt);
        // Destructor writes trailer and releases AVIO, without an explicit close.
    }
    AVFormatContext *input = nullptr;
    CHECK(avformat_open_input(&input, output.c_str(), nullptr, nullptr) == 0);
    AVPacket *pkt = av_packet_alloc(); CHECK(pkt);
    CHECK(av_read_frame(input, pkt) == 0);
    CHECK(input->streams[pkt->stream_index]->codecpar->width == 16);
    CHECK(av_rescale_q(pkt->pts, input->streams[pkt->stream_index]->time_base,
                       AVRational{1, 1000}) == 1000);
    av_packet_free(&pkt); avformat_close_input(&input);
    FFMuxer mux;
    CHECK(mux.init("unused", "null") == 0); streams(mux); CHECK(mux.writeHeader() == 0);
    const int index = mux.getVStreamIndex();
    auto writer = std::async(std::launch::async, [&] {
        for (int i = 0; i < 1000; ++i) {
            AVPacket packet{}; packet.stream_index = index;
            packet.pts = packet.dts = i; packet.duration = 1;
            const int ret = mux.mux(&packet);
            if (ret < 0) return ret;
        }
        return 0;
    });
    CHECK(mux.close() == 0); CHECK(mux.close() == 0);
    const int result = writer.get();
    CHECK(result == 0 || result == AVERROR(EINVAL));
}
int main()
{
    try {
        // Unique retained directory: never overwrite user files or delete artifacts.
        const QString root = QDir::currentPath() + "/muxer-output-" +
                             QUuid::createUuid().toString(QUuid::WithoutBraces);
        CHECK(QDir().mkpath(root));
        emptyAndPreconditions(); waits(); failures(root); freedContextsAndCloseRace(root);
        std::cout << "PASS: muxer lifecycle; artifacts=" << root.toStdString() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}

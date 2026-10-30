#include "aac_priming_fixture.h"
#include "queue/recordersyncqueue.h"
#include "queue/ffvpacketqueue.h"
#include "thread/ffmuxerthread.h"

namespace {
void run(const std::filesystem::path &path, FFMuxer &mux, RecorderSyncQueue &clock) {
    clock.reset(false);
    clock.configureVideo({30,1}, {1,30});
    FFVPacketQueue video;
    video.start();
    Codec v(avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_H264)));
    CHECK(v && v->codec);
    v->width = v->height = 16; v->pix_fmt = AV_PIX_FMT_YUV420P;
    v->time_base = {1,30}; v->framerate = {30,1};
    v->extradata_size = 33;
    v->extradata = static_cast<uint8_t *>(av_mallocz(33 + AV_INPUT_BUFFER_PADDING_SIZE));
    CHECK(v->extradata); std::memcpy(v->extradata, h264, 33);
    CHECK(mux.init(path.string(), "mp4", false) == 0);
    CHECK(mux.addStream(v.get()) == 0);
    FFMuxerThread worker;
    worker.init(nullptr, &video, &mux, nullptr, nullptr, nullptr);
    worker.start();
    struct Cleanup {
        FFMuxerThread &worker;
        ~Cleanup() { worker.requestAbort(); worker.wait(); }
    } cleanup{worker};
    for (int n = 0; n < 90; ++n) {
        if (n == 45) {
            clock.onPauseChanged(true, 1500000);
            for (int i = 0; i < 30; ++i) CHECK(clock.reserveVideoFrame().drop);
            clock.onPauseChanged(false, 2500000);
        }
        const auto timing = clock.reserveVideoFrame();
        CHECK(!timing.drop && !timing.audioClockReady && timing.pts == n);
        Packet packet(av_packet_alloc());
        CHECK(packet && av_new_packet(packet.get(), sizeof(h264)) == 0);
        std::memcpy(packet->data, h264, sizeof(h264));
        packet->stream_index = mux.getVStreamIndex();
        packet->pts = packet->dts = timing.pts;
        packet->duration = 1; packet->flags = AV_PKT_FLAG_KEY;
        video.enqueue(packet.get());
    }
    CHECK(video.sendFinish() == MediaQueueResult::Eof);
    CHECK(worker.waitFor(std::chrono::seconds(3)));
    CHECK(worker.lastError() == 0 && !worker.drainTimedOut());
    CHECK(mux.close() == 0);

    AVFormatContext *input = nullptr;
    CHECK(avformat_open_input(&input, path.string().c_str(), nullptr, nullptr) == 0);
    struct Input { AVFormatContext *p; ~Input() { avformat_close_input(&p); } } guard{input};
    CHECK(input->nb_streams == 1);
    auto *stream = input->streams[0];
    CHECK(stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO);
    CHECK(std::abs(stream->duration * av_q2d(stream->time_base) - 3.0) < .001);
    Codec decoder(avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_H264)));
    CHECK(decoder && avcodec_parameters_to_context(decoder.get(), stream->codecpar) == 0);
    CHECK(avcodec_open2(decoder.get(), decoder->codec, nullptr) == 0);
    Packet packet(av_packet_alloc());
    std::unique_ptr<AVFrame,FrameDelete> frame(av_frame_alloc());
    CHECK(packet && frame);
    int count = 0, decoded = 0;
    auto drain = [&] {
        int ret;
        while ((ret = avcodec_receive_frame(decoder.get(), frame.get())) >= 0) {
            CHECK(frame->width == 16 && frame->height == 16);
            ++decoded; av_frame_unref(frame.get());
        }
        CHECK(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
    };
    int ret;
    while ((ret = av_read_frame(input, packet.get())) == 0) {
        CHECK(packet->stream_index == 0);
        CHECK(av_rescale_q(packet->pts, stream->time_base, (AVRational{1,30})) == count);
        CHECK(packet->pts == packet->dts); ++count;
        CHECK(avcodec_send_packet(decoder.get(), packet.get()) == 0); drain();
        av_packet_unref(packet.get());
    }
    CHECK(ret == AVERROR_EOF);
    CHECK(avcodec_send_packet(decoder.get(), nullptr) == 0); drain();
    CHECK(count == 90 && decoded == 90);
    // A later audio-enabled session must regain its absent-audio startup limit.
    clock.reset(); clock.configureVideo({30,1}, {1,30});
    bool limited = false;
    for (int n = 0; n < 30; ++n) limited |= clock.reserveVideoFrame().drop;
    CHECK(limited);
}
}
int main() {
    try {
        const auto root = std::filesystem::temp_directory_path() / ("subrec-video-only-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        CHECK(std::filesystem::create_directory(root));
        FFMuxer mux; RecorderSyncQueue clock;
        run(root / "first.mp4", mux, clock);
        run(root / "restart.mp4", mux, clock);
        std::cout << "PASS: video-only MP4, 90 decoded frames per session, pause/resume and restart\n";
    } catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

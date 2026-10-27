#include "aac_priming_fixture.h"
#include "encoder/ffaencoder.h"
#include "queue/ffapacketqueue.h"
#include "queue/ffvpacketqueue.h"
#include "thread/ffmuxerthread.h"
#include <thread>

namespace {
void runPipeline(const std::filesystem::path &root, int videoStart, int reorder,
                 bool delayedVideo) {
    const auto path = (root / ("pipeline-video-" + std::to_string(videoStart) +
        "-reorder-" + std::to_string(reorder) + (delayedVideo ? "-delayed" : "") + ".mp4")).string();
    FFAPacketQueue audio;
    FFVPacketQueue video;
    audio.start(); video.start();
    FFAEncoder encoder; encoder.init(&audio);
    std::unique_ptr<AVFrame,FrameDelete> frame(av_frame_alloc()); CHECK(frame);
    frame->format = AV_SAMPLE_FMT_FLTP; frame->sample_rate = 48000;
    av_channel_layout_default(&frame->ch_layout, 2);
    frame->nb_samples = 1024; CHECK(av_frame_get_buffer(frame.get(), 0) == 0);
    encoder.initAudio(frame.get());
    CHECK(encoder.getCodecCtx() && encoder.getCodecCtx()->initial_padding == 1024);
    Codec v(avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_H264)));
    CHECK(v && v->codec);
    v->width = v->height = 16; v->pix_fmt = AV_PIX_FMT_YUV420P;
    v->time_base = {1,30}; v->framerate = {30,1};
    v->extradata_size = 33;
    v->extradata = static_cast<uint8_t *>(av_mallocz(33 + AV_INPUT_BUFFER_PADDING_SIZE));
    CHECK(v->extradata); std::memcpy(v->extradata, h264, 33);
    FFMuxer mux; CHECK(mux.init(path) == 0);
    // Match either order of asynchronous encoder initialization.
    if (reorder) {
        CHECK(mux.addStream(v.get()) == 0); CHECK(mux.addStream(encoder.getCodecCtx()) == 0);
    } else {
        CHECK(mux.addStream(encoder.getCodecCtx()) == 0); CHECK(mux.addStream(v.get()) == 0);
    }
    const int ai = mux.getAStreamIndex(), vi = mux.getVStreamIndex();
    auto encodeFrame = [&](int n) {
        // Same input timeline as RecorderSyncQueue / FFAEncoderThread: first
        // input sample is 0; AAC introduces its own negative output timestamp.
        frame->pts = n * 1024;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 1024; ++i)
            reinterpret_cast<float *>(frame->data[ch])[i] =
                .2f * std::sin(6.283185307179586 * 440 * (n*1024+i) / 48000);
        CHECK(encoder.encode(frame.get(), ai, frame->pts, {1,48000}) == 0);
    };
    encodeFrame(0); encodeFrame(1);
    CHECK(audio.peekQueue());
    CHECK(audio.peekQueue()->packet.pts == -1024 && audio.peekQueue()->packet.dts == -1024);
    FFMuxerThread worker; worker.init(&audio, &video, &mux, &encoder, nullptr, nullptr);
    worker.start();
    std::exception_ptr producerError;
    std::thread producer([&] {
        try {
            for (int n = 2; n < 96; ++n) encodeFrame(n);
            encoder.finish(ai);
        } catch (...) {
            producerError = std::current_exception(); worker.requestAbort();
        }
    });
    struct Cleanup {
        FFMuxerThread &worker; std::thread &producer;
        ~Cleanup() { worker.requestAbort(); if (producer.joinable()) producer.join(); worker.wait(); }
    } cleanup{worker, producer};
    // The old worker consumed one audio packet per 5 ms and discarded everything
    // beyond 24 packets. 400 ms exercises both that loss and queue backpressure.
    if (delayedVideo) std::this_thread::sleep_for(std::chrono::milliseconds(400));
    for (int n = 0; n < 30; ++n) {
        Packet p(av_packet_alloc()); CHECK(p && av_new_packet(p.get(), sizeof(h264)) == 0);
        std::memcpy(p->data, h264, sizeof(h264)); p->stream_index = vi;
        p->pts = videoStart+n; p->dts = p->pts-reorder; p->duration = 1; p->flags = AV_PKT_FLAG_KEY;
        video.enqueue(p.get());
    }
    CHECK(video.sendFinish() == MediaQueueResult::Eof);
    producer.join(); worker.wait();
    if (producerError) std::rethrow_exception(producerError);
    CHECK(worker.lastError() == 0); CHECK(mux.close() == 0);
    checkRecording(path, videoStart, reorder);
    AVFormatContext *input = nullptr;
    CHECK(avformat_open_input(&input, path.c_str(), nullptr, nullptr) == 0);
    Packet p(av_packet_alloc()); CHECK(p);
    int audioCount = 0, videoCount = 0;
    while (av_read_frame(input, p.get()) == 0) {
        auto *st = input->streams[p->stream_index];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            CHECK(av_rescale_q(p->pts, st->time_base, {1,48000}) == (audioCount-1)*1024);
            ++audioCount;
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) ++videoCount;
        av_packet_unref(p.get());
    }
    avformat_close_input(&input);
    CHECK(audioCount == 97 && videoCount == 30);
    std::cout << "PASS: production AAC queue/mux thread: first PTS=-1024, elst=1024, audio start=0, "
              << "packets=" << audioCount << "/" << videoCount << ", video start=" << videoStart
              << "/30 reorder=" << reorder << "/30, delayed=" << delayedVideo << ", " << path << '\n';
}
}
int main() {
    try {
        auto root = std::filesystem::temp_directory_path() / ("subrec-aac-pipeline-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        CHECK(std::filesystem::create_directory(root));
        runPipeline(root, 0, 0, false);
        runPipeline(root, 0, 2, false);
        runPipeline(root, 3, 0, false);
        runPipeline(root, 3, 2, true);
    } catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

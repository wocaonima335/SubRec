#include "aac_priming_fixture.h"
#include "encoder/ffaencoder.h"
#include "queue/ffapacketqueue.h"
#include "queue/ffvpacketqueue.h"
#include "thread/ffmuxerthread.h"
#include "capture/test_video_start_delay.h"
#include <thread>

namespace {
void runPipeline(const std::filesystem::path &root, int videoStart, int reorder,
                 bool delayedVideo, int stalled = 0) {
    const auto path = (root / ("pipeline-video-" + std::to_string(videoStart) +
        "-reorder-" + std::to_string(reorder) + (delayedVideo ? "-delayed" : "") + "-stall-" + std::to_string(stalled) + ".mp4")).string();
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
    const int audioFrames = stalled ? 16 : 96;
    std::exception_ptr producerError;
    std::thread producer([&] {
        try {
            for (int n = 2; n < audioFrames; ++n) encodeFrame(n);
            if (stalled == 1 || stalled == 3 || stalled == 4) {
                CHECK(encoder.encode(nullptr, ai, AV_NOPTS_VALUE, {1,48000}) == 0);
                // Fake stalled source: all currently available packets, but no EOF.
            } else encoder.finish(ai);
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
    capture::test::VideoStartDelay delay(std::chrono::milliseconds(delayedVideo ? 400 : 0));
    CHECK(delay.beforeFirstPacket([](auto duration) {
        std::this_thread::sleep_for(duration); return false;
    }, [](auto ms) { CHECK(ms == 400); }));
    for (int n = 0; n < (stalled == 4 ? 0 : 30); ++n) {
        Packet p(av_packet_alloc()); CHECK(p && av_new_packet(p.get(), sizeof(h264)) == 0);
        std::memcpy(p->data, h264, sizeof(h264)); p->stream_index = vi;
        p->pts = videoStart+n; p->dts = p->pts-reorder; p->duration = 1; p->flags = AV_PKT_FLAG_KEY;
        video.enqueue(p.get());
    }
    if (stalled != 2 && stalled != 3 && stalled != 4)
        CHECK(video.sendFinish() == MediaQueueResult::Eof);
    producer.join();
    if (stalled) {
        // Silence during recording must not auto-finalize; arm only at Stop.
        CHECK(!worker.waitFor(std::chrono::milliseconds(40)));
        const auto start = std::chrono::steady_clock::now();
        worker.requestDrainStop(std::chrono::milliseconds(120));
        worker.requestDrainStop(std::chrono::seconds(5)); // later stages cannot extend the deadline
        CHECK(worker.waitFor(std::chrono::seconds(2)));
        const auto ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        CHECK(ms >= 100 && ms < 1000 && worker.drainTimedOut());
        CHECK(audio.isFinished() && video.isFinished());
        std::cout << "PASS: stalled=" << stalled << " stop/finalize ms=" << ms << '\n';
    } else worker.wait();
    if (producerError) std::rethrow_exception(producerError);
    CHECK(worker.lastError() == 0); CHECK(mux.close() == 0);
    const auto diagnostics = worker.diagnostics();
    CHECK(diagnostics.droppedPackets == 0 && diagnostics.bootstrapMaxPackets <= 24);
    if (delayedVideo) {
        CHECK(diagnostics.bootstrapMaxPackets == 24 && diagnostics.bootstrapHoldNs > 0);
        CHECK(diagnostics.enqueueBlockedNs > 0);
        std::cout << "PASS: mux diagnostics bootstrap_max=" << diagnostics.bootstrapMaxPackets
                  << " hold_ms=" << diagnostics.bootstrapHoldNs/1000000.0
                  << " backpressure_ms=" << diagnostics.enqueueBlockedNs/1000000.0
                  << " dropped=" << diagnostics.droppedPackets << '\n';
    }
    worker.close(); // exercises the one-shot close summary
    CHECK(worker.diagnostics().droppedPackets == 0);
    if (stalled != 4) checkRecording(path, videoStart, reorder);
    else {
        std::ifstream file(path, std::ios::binary); CHECK(file.good());
        Bytes bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        const auto track = child(bytes, child(bytes, {0,bytes.size(),""}, "moov"), "trak");
        const auto elst = child(bytes, child(bytes, track, "edts"), "elst");
        CHECK(bytes[elst.data] == 0 && number(bytes, elst.data+12, 4) == 1024);
    }
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
    CHECK(audioCount == audioFrames+1 && videoCount == (stalled == 4 ? 0 : 30));
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
        runPipeline(root, 3, 2, false, 1); // missing audio EOF
        runPipeline(root, 0, 2, false, 2); // missing video EOF
        runPipeline(root, 0, 0, false, 3); // neither sends EOF
        runPipeline(root, 0, 0, false, 4); // no video packet, bootstrap wait
    } catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

// P2: IMuxSink finalize states, finalize budget versus I/O interruption, and
// hybrid_fragmented recoverability when the process dies without a trailer.
#include "aac_priming_fixture.h"

#include <chrono>
#include <thread>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
using namespace std::chrono_literals;

Codec videoContext()
{
    Codec v(avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_H264)));
    CHECK(v && v->codec);
    v->width = v->height = 16; v->pix_fmt = AV_PIX_FMT_YUV420P;
    v->time_base = {1, 30}; v->framerate = {30, 1};
    v->extradata_size = 33; // Annex B SPS and PPS
    v->extradata = static_cast<uint8_t *>(av_mallocz(v->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE));
    CHECK(v->extradata); std::memcpy(v->extradata, h264, v->extradata_size);
    return v;
}

Codec audioEncoder()
{
    Codec a(avcodec_alloc_context3(avcodec_find_encoder_by_name("aac")));
    CHECK(a && a->codec);
    a->sample_rate = 48000; a->sample_fmt = AV_SAMPLE_FMT_FLTP; a->bit_rate = audio_encoding::kAacBitRate;
    a->time_base = {1, 48000}; av_channel_layout_default(&a->ch_layout, 2);
    a->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    CHECK(avcodec_open2(a.get(), a->codec, nullptr) == 0);
    return a;
}

MuxWriteResult writeVideo(FFMuxer &mux, int64_t tick)
{
    Packet p(av_packet_alloc());
    CHECK(p && av_new_packet(p.get(), sizeof(h264)) == 0);
    std::memcpy(p->data, h264, sizeof(h264));
    p->stream_index = mux.getVStreamIndex();
    p->pts = p->dts = tick; p->duration = 1; p->flags = AV_PKT_FLAG_KEY;
    return mux.write(p.get());
}

std::vector<std::string> topLevelBoxes(const std::string &path)
{
    std::ifstream file(path, std::ios::binary); CHECK(file.good());
    Bytes b{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    // Tolerant scan: a crashed file may end inside its last box.
    std::vector<std::string> types;
    size_t pos = 0;
    while (b.size() - pos >= 8) {
        uint64_t size = number(b, pos, 4);
        size_t header = 8;
        if (size == 1 && b.size() - pos >= 16) { size = number(b, pos + 8, 8); header = 16; }
        if (size == 0) size = b.size() - pos;
        if (size < header) break;
        types.emplace_back(b.begin() + pos + 4, b.begin() + pos + 8);
        if (size > b.size() - pos) break;
        pos += size;
    }
    return types;
}

bool contains(const std::vector<std::string> &types, const char *type)
{
    return std::find(types.begin(), types.end(), type) != types.end();
}

int64_t countVideoPackets(const std::string &path)
{
    AVFormatContext *input = nullptr;
    CHECK(avformat_open_input(&input, path.c_str(), nullptr, nullptr) == 0);
    CHECK(avformat_find_stream_info(input, nullptr) >= 0);
    Packet p(av_packet_alloc()); CHECK(p);
    int64_t count = 0;
    while (av_read_frame(input, p.get()) == 0) {
        if (input->streams[p->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) ++count;
        av_packet_unref(p.get());
    }
    avformat_close_input(&input);
    return count;
}

void finalizedIsRegularMp4(const std::filesystem::path &root)
{
    const std::string path = (root / "finalized.mp4").string();
    FFMuxer mux;
    CHECK(mux.finalizeState() == FinalizeState::NotFinalized);
    CHECK(mux.init(path, "mp4", false) == 0);
    auto v = videoContext(); CHECK(mux.addStream(v.get()) == 0);
    CHECK(mux.writeHeader() == 0);
    for (int tick = 0; tick < 150; ++tick) CHECK(writeVideo(mux, tick) == MuxWriteResult::Ok);
    CHECK(mux.writtenPackets() == 150 && mux.timescaleMismatches() == 0);
    CHECK(mux.finalize(5s) == FinalizeState::Finalized);
    CHECK(mux.finalize(5s) == FinalizeState::Finalized);   // repeated calls return the first result
    CHECK(mux.close() == 0);
    CHECK(mux.finalizeState() == FinalizeState::Finalized);
    const auto types = topLevelBoxes(path);
    // hybrid_fragmented converts to a regular file: moov present, no top-level moof.
    CHECK(contains(types, "moov") && contains(types, "mdat") && !contains(types, "moof"));
    CHECK(countVideoPackets(path) == 150);
    // init() starts a new session state.
    CHECK(mux.init((root / "second.mp4").string(), "mp4", false) == 0);
    CHECK(mux.finalizeState() == FinalizeState::NotFinalized && mux.writtenPackets() == 0);
    mux.close();
}

void noPacketsIsFailedNoData(const std::filesystem::path &root)
{
    {
        FFMuxer mux;
        CHECK(mux.init((root / "empty.mp4").string(), "mp4", false) == 0);
        auto v = videoContext(); CHECK(mux.addStream(v.get()) == 0);
        CHECK(mux.writeHeader() == 0);
        CHECK(mux.finalize(5s) == FinalizeState::FailedNoData);
    }
    {
        // Header never written (stream wait aborted).
        FFMuxer mux;
        CHECK(mux.init((root / "no-header.mp4").string(), "mp4", false) == 0);
        mux.requestAbort();
        CHECK(mux.writeHeader(10ms) == AVERROR_EXIT);
        CHECK(mux.finalize(5s) == FinalizeState::FailedNoData);
    }
    {
        FFMuxer never;   // never initialized: nothing to finalize
        CHECK(never.finalize(5s) == FinalizeState::NotFinalized);
        CHECK(never.close() == 0);
    }
}

void abortAfterDataStillWritesTrailer(const std::filesystem::path &root)
{
    const std::string path = (root / "abort-after-data.mp4").string();
    FFMuxer mux;
    CHECK(mux.init(path, "mp4", false) == 0);
    auto v = videoContext(); CHECK(mux.addStream(v.get()) == 0);
    CHECK(mux.writeHeader() == 0);
    for (int tick = 0; tick < 60; ++tick) CHECK(writeVideo(mux, tick) == MuxWriteResult::Ok);
    mux.requestAbort();                                    // stops accepting packets...
    CHECK(writeVideo(mux, 60) == MuxWriteResult::Fatal);
    CHECK(mux.finalize(5s) == FinalizeState::Finalized);   // ...but the trailer is still written
    CHECK(contains(topLevelBoxes(path), "moov"));
    CHECK(countVideoPackets(path) == 60);
}

void expiredBudgetIsRecoverable(const std::filesystem::path &root)
{
    const std::string path = (root / "expired-budget.mp4").string();
    FFMuxer mux;
    mux.setFinalizeBudget(1ms);
    CHECK(mux.init(path, "mp4", false) == 0);
    auto v = videoContext(); CHECK(mux.addStream(v.get()) == 0);
    CHECK(mux.writeHeader() == 0);
    for (int tick = 0; tick < 60; ++tick) CHECK(writeVideo(mux, tick) == MuxWriteResult::Ok);
    // requestAbort() after data arms the (1 ms) finalize budget; once it has expired
    // the I/O interrupt fires, as for a disk that stalls beyond the stop timeout.
    mux.requestAbort();
    std::this_thread::sleep_for(20ms);
    CHECK(mux.finalize(5s) == FinalizeState::Recoverable);   // earlier deadline is kept
}

void skippedPacketsAreNotFatal()
{
    FFMuxer mux;
    CHECK(mux.init("unused", "null", false) == 0);
    auto v = videoContext(); CHECK(mux.addStream(v.get()) == 0);
    CHECK(mux.writeHeader() == 0);
    CHECK(writeVideo(mux, 5) == MuxWriteResult::Ok);
    for (int i = 0; i < 29; ++i) CHECK(writeVideo(mux, 5) == MuxWriteResult::Skipped);
    CHECK(writeVideo(mux, 6) == MuxWriteResult::Ok);   // an accepted packet resets the run
    for (int i = 0; i < 29; ++i) CHECK(writeVideo(mux, 6) == MuxWriteResult::Skipped);
    CHECK(writeVideo(mux, 6) == MuxWriteResult::Fatal);  // 30 in a row: output declared broken
    CHECK(mux.skippedPackets() == 59);
    CHECK(mux.mux(nullptr) < 0);
}

#ifndef _WIN32
// Child writes 20 s of AAC + keyframe video every 2 s, then dies without a trailer.
[[noreturn]] void crashingWriter(const std::string &path)
{
    try {
        auto a = audioEncoder();
        auto v = videoContext();
        static FFMuxer mux;   // deliberately never destroyed: _exit skips the trailer
        CHECK(mux.init(path) == 0);
        CHECK(mux.addStream(a.get()) == 0); CHECK(mux.addStream(v.get()) == 0);
        CHECK(mux.writeHeader() == 0);
        int64_t nextVideoTick = 0;
        const int frames = 20 * 48000 / 1024;
        for (int n = 0; n <= frames; ++n) {
            std::unique_ptr<AVFrame, FrameDelete> f(av_frame_alloc()); CHECK(f);
            f->format = a->sample_fmt; f->sample_rate = a->sample_rate;
            CHECK(av_channel_layout_copy(&f->ch_layout, &a->ch_layout) == 0);
            f->nb_samples = 1024; f->pts = int64_t(n) * 1024; CHECK(av_frame_get_buffer(f.get(), 0) == 0);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 1024; ++i)
                reinterpret_cast<float *>(f->data[ch])[i] = .2f * std::sin(6.283185307179586 * 440 * (n * 1024 + i) / 48000);
            CHECK(avcodec_send_frame(a.get(), f.get()) == 0);
            for (;;) {
                Packet p(av_packet_alloc()); CHECK(p);
                if (avcodec_receive_packet(a.get(), p.get()) < 0) break;
                // Interleave a keyframe whenever video is due before this audio packet.
                while (av_compare_ts(nextVideoTick, v->time_base, p->dts, a->time_base) <= 0) {
                    CHECK(writeVideo(mux, nextVideoTick) == MuxWriteResult::Ok);
                    nextVideoTick += 60;
                }
                p->stream_index = mux.getAStreamIndex();
                CHECK(mux.write(p.get()) == MuxWriteResult::Ok);
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "child FAIL: " << e.what() << '\n';
        _exit(2);
    }
    _exit(0);
}

void crashLeavesPlayableFragments(const std::filesystem::path &root)
{
    const std::string path = (root / "crashed.mp4").string();
    const pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) crashingWriter(path);
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    const auto types = topLevelBoxes(path);
    CHECK(contains(types, "moof"));   // fragmented while recording
    AVFormatContext *input = nullptr;
    CHECK(avformat_open_input(&input, path.c_str(), nullptr, nullptr) == 0);
    CHECK(avformat_find_stream_info(input, nullptr) >= 0);
    Packet p(av_packet_alloc()); CHECK(p);
    double lastAudio = 0;
    int64_t video = 0;
    while (av_read_frame(input, p.get()) == 0) {
        auto *st = input->streams[p->stream_index];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            lastAudio = std::max(lastAudio, (p->pts + p->duration) * av_q2d(st->time_base));
        else ++video;
        av_packet_unref(p.get());
    }
    avformat_close_input(&input);
    // Lost at most the open fragment (frag_duration 2 s); flush_packets leaves no
    // completed fragment in the AVIO buffer.
    std::cout << "crash recovery: decodable audio " << lastAudio << " s of 20 s, video packets " << video << '\n';
    CHECK(lastAudio >= 17.5 && video >= 9);
}
#endif
}

int main()
{
    try {
        auto root = std::filesystem::temp_directory_path() / ("subrec-finalize-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        CHECK(std::filesystem::create_directory(root));
        finalizedIsRegularMp4(root);
        noPacketsIsFailedNoData(root);
        abortAfterDataStillWritesTrailer(root);
        expiredBudgetIsRecoverable(root);
        skippedPacketsAreNotFatal();
#ifndef _WIN32
        crashLeavesPlayableFragments(root);
#else
        std::cout << "SKIP: crash recovery needs fork()\n";
#endif
        std::cout << "PASS: finalize states; artifacts=" << root.string() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

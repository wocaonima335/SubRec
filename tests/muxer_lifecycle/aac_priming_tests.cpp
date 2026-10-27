#include "muxer/ffmuxer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
extern "C" {
#include <libavutil/intreadwrite.h>
}
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__)); } while (false)
namespace {
struct CodecDelete { void operator()(AVCodecContext *p) const { avcodec_free_context(&p); } };
struct FrameDelete { void operator()(AVFrame *p) const { av_frame_free(&p); } };
struct PacketDelete { void operator()(AVPacket *p) const { av_packet_free(&p); } };
using Codec = std::unique_ptr<AVCodecContext, CodecDelete>;
using Packet = std::unique_ptr<AVPacket, PacketDelete>;
// Real 16x16 black H.264 IDR, generated with libx264 ultrafast/zerolatency at 30 fps.
// Annex B SPS/PPS/IDR only (SEI removed). No GPU or video encoder needed for this
// container timing test; the same valid intra picture is used at each video PTS.
constexpr uint8_t h264[] = {
    0,0,0,1,0x67,0x42,0xc0,0x0a,0xda,0x7b,0x01,0x10,0,0,3,0,0x10,0,0,3,3,0xc8,0xf1,0x22,0x6a,
    0,0,0,1,0x68,0xce,0x0f,0xc8,0,0,0,1,0x65,0x88,0x84,0x3a,0x26,0x28,0,9,2,0xe0
};
struct Box { size_t data, end; std::string type; };
using Bytes = std::vector<uint8_t>;
uint64_t number(const Bytes &b, size_t pos, size_t size) {
    CHECK(pos <= b.size() && size <= b.size() - pos);
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i) value = (value << 8) | b[pos+i];
    return value;
}
std::vector<Box> boxes(const Bytes &b, size_t begin, size_t end) {
    CHECK(end <= b.size()); std::vector<Box> result;
    while (begin < end) {
        CHECK(end - begin >= 8);
        uint64_t size = number(b, begin, 4); size_t header = 8;
        std::string type(b.begin()+begin+4, b.begin()+begin+8);
        if (size == 1) { CHECK(end-begin >= 16); size = number(b, begin+8, 8); header = 16; }
        if (size == 0) size = end-begin;
        CHECK(size >= header && size <= end-begin);
        result.push_back({begin+header, begin+size, type}); begin += size;
    }
    return result;
}
Box child(const Bytes &b, Box parent, const char *type) {
    for (auto box : boxes(b, parent.data, parent.end)) if (box.type == type) return box;
    throw std::runtime_error(std::string("missing MP4 box: ") + type);
}
void checkEdits(const std::string &path, int videoStart, int reorder) {
    std::ifstream file(path, std::ios::binary); CHECK(file.good());
    Bytes b{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    auto moov = child(b, {0,b.size(),""}, "moov");
    bool audio = false, video = false;
    for (auto track : boxes(b, moov.data, moov.end)) {
        if (track.type != "trak") continue;
        auto mdia = child(b, track, "mdia"), hdlr = child(b, mdia, "hdlr");
        CHECK(hdlr.end - hdlr.data >= 12);
        std::string type(b.begin()+hdlr.data+8, b.begin()+hdlr.data+12);
        if (type != "soun" && type != "vide") continue;
        auto mdhd = child(b, mdia, "mdhd");
        const auto scale = number(b, mdhd.data + (b[mdhd.data] == 1 ? 20 : 12), 4);
        auto elst = child(b, child(b, track, "edts"), "elst");
        CHECK(elst.end-elst.data >= 8 && b[elst.data] <= 1);
        bool wide = b[elst.data] == 1;
        size_t width = wide ? 8 : 4, stride = wide ? 20 : 12;
        auto count = number(b, elst.data+4, 4);
        CHECK(count * stride == elst.end-elst.data-8);
        CHECK(count == (type == "vide" && videoStart > 0 ? 2 : 1));
        size_t last = elst.data + 8 + (count-1)*stride;
        auto media = number(b, last+width, width);
        CHECK(number(b, last+2*width, 4) == 0x10000); // normal playback rate
        if (type == "soun") {
            CHECK(scale == 48000 && media == 1024); audio = true;
        } else {
            CHECK(media == uint64_t(av_rescale_q(reorder, {1,30}, {1,int(scale)})));
            video = true;
        }
    }
    CHECK(audio && video);
}
void run(const std::filesystem::path &root, int videoStart, int reorder, bool videoFirst) {
    auto name = "aac-video-" + std::to_string(videoStart) + "-reorder-" + std::to_string(reorder) + ".mp4";
    const std::string path = (root / name).string();
    Codec a(avcodec_alloc_context3(avcodec_find_encoder_by_name("aac")));
    CHECK(a && a->codec);
    a->sample_rate = 48000; a->sample_fmt = AV_SAMPLE_FMT_FLTP; a->bit_rate = 64*1024;
    a->time_base = {1,48000}; av_channel_layout_default(&a->ch_layout, 2);
    a->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    CHECK(avcodec_open2(a.get(), a->codec, nullptr) == 0);
    CHECK(a->initial_padding == 1024 && a->frame_size == 1024);
    // addStream needs codec/type metadata, not an opened encoder, for copied video packets.
    Codec v(avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_H264)));
    CHECK(v && v->codec);
    v->width = v->height = 16; v->pix_fmt = AV_PIX_FMT_YUV420P;
    v->time_base = {1,30}; v->framerate = {30,1};
    v->extradata_size = 33; // Annex B SPS and PPS
    v->extradata = static_cast<uint8_t *>(av_mallocz(v->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE));
    CHECK(v->extradata); std::memcpy(v->extradata, h264, v->extradata_size);
    FFMuxer mux; CHECK(mux.init(path) == 0);
    if (videoFirst) { CHECK(mux.addStream(v.get()) == 0); CHECK(mux.addStream(a.get()) == 0); }
    else { CHECK(mux.addStream(a.get()) == 0); CHECK(mux.addStream(v.get()) == 0); }
    CHECK(mux.writeHeader() == 0);
    std::vector<Packet> packets;
    auto drain = [&] {
        int ret;
        for (;;) {
            Packet p(av_packet_alloc()); CHECK(p);
            ret = avcodec_receive_packet(a.get(), p.get());
            if (ret < 0) break;
            p->stream_index = mux.getAStreamIndex(); packets.push_back(std::move(p));
        }
        CHECK(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
    };
    for (int n = 0; n < 48; ++n) {
        std::unique_ptr<AVFrame,FrameDelete> f(av_frame_alloc()); CHECK(f);
        f->format = a->sample_fmt; f->sample_rate = a->sample_rate;
        CHECK(av_channel_layout_copy(&f->ch_layout, &a->ch_layout) == 0);
        f->nb_samples = 1024; f->pts = n*1024; CHECK(av_frame_get_buffer(f.get(), 0) == 0);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 1024; ++i)
            reinterpret_cast<float *>(f->data[ch])[i] = .2f * std::sin(6.283185307179586 * 440 * (n*1024+i) / 48000);
        CHECK(avcodec_send_frame(a.get(), f.get()) == 0); drain();
    }
    CHECK(avcodec_send_frame(a.get(), nullptr) == 0); drain();
    CHECK(packets.size() > 1 && packets.front()->pts == -1024 && packets.front()->dts == -1024);
    CHECK(packets[1]->pts == 0);
    for (int i = 0; i < 30; ++i) {
        Packet p(av_packet_alloc()); CHECK(p && av_new_packet(p.get(), sizeof(h264)) == 0);
        std::memcpy(p->data, h264, sizeof(h264)); p->stream_index = mux.getVStreamIndex();
        p->pts = videoStart+i; p->dts = p->pts-reorder; p->duration = 1; p->flags = AV_PKT_FLAG_KEY;
        packets.push_back(std::move(p));
    }
    std::stable_sort(packets.begin(), packets.end(), [&](const Packet &x, const Packet &y) {
        return av_compare_ts(x->dts, x->stream_index == mux.getAStreamIndex() ? a->time_base : v->time_base,
                             y->dts, y->stream_index == mux.getAStreamIndex() ? a->time_base : v->time_base) < 0;
    });
    for (auto &p : packets) CHECK(mux.mux(p.get()) == 0);
    CHECK(mux.writeTrailer() == 0); CHECK(mux.close() == 0);
    checkEdits(path, videoStart, reorder);
    AVFormatContext *input = nullptr;
    CHECK(avformat_open_input(&input, path.c_str(), nullptr, nullptr) == 0);
    bool foundAudio = false, foundVideo = false;
    Packet p(av_packet_alloc()); CHECK(p);
    while (av_read_frame(input, p.get()) == 0) {
        auto *st = input->streams[p->stream_index];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && !foundAudio) {
            CHECK(st->start_time != AV_NOPTS_VALUE && std::abs(av_q2d(st->time_base)*st->start_time) < 1.0/48000);
            CHECK(av_rescale_q(p->pts, st->time_base, a->time_base) == -1024);
            size_t size = 0; auto *skip = av_packet_get_side_data(p.get(), AV_PKT_DATA_SKIP_SAMPLES, &size);
            CHECK(skip && size >= 10 && AV_RL32(skip) == 1024); foundAudio = true;
        }
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !foundVideo) {
            CHECK(av_rescale_q(p->pts, st->time_base, v->time_base) == videoStart);
            CHECK(av_rescale_q(p->dts, st->time_base, v->time_base) == videoStart-reorder);
            CHECK(std::abs(av_q2d(st->time_base)*st->start_time - videoStart/30.0) < .001);
            foundVideo = true;
        }
        av_packet_unref(p.get());
    }
    CHECK(foundAudio && foundVideo); avformat_close_input(&input);
    std::cout << "PASS: elst media_time=1024, audio start=0, video start=" << videoStart
              << "/30 reorder=" << reorder << "/30, " << path << '\n';
}
}
int main() {
    try {
        auto root = std::filesystem::temp_directory_path() / ("subrec-aac-priming-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        CHECK(std::filesystem::create_directory(root));
        run(root, 0, 0, false); run(root, 0, 2, true); run(root, 3, 0, true);
        // Retain the MP4 fixtures for PowerShell/PC2 parser verification.
    } catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

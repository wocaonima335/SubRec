#pragma once
#include "muxer/ffmuxer.h"
#include "encoder/audio_encoding_config.h"
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
void checkRecording(const std::string &path, int videoStart, int reorder) {
    checkEdits(path, videoStart, reorder);
    AVFormatContext *input = nullptr;
    CHECK(avformat_open_input(&input, path.c_str(), nullptr, nullptr) == 0);
    bool foundAudio = false, foundVideo = false;
    Packet p(av_packet_alloc()); CHECK(p);
    while (av_read_frame(input, p.get()) == 0) {
        auto *st = input->streams[p->stream_index];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && !foundAudio) {
            CHECK(st->start_time != AV_NOPTS_VALUE && std::abs(av_q2d(st->time_base)*st->start_time) < 1.0/48000);
            CHECK(av_rescale_q(p->pts, st->time_base, (AVRational{1,48000})) == -1024);
            size_t size = 0; auto *skip = av_packet_get_side_data(p.get(), AV_PKT_DATA_SKIP_SAMPLES, &size);
            CHECK(skip && size >= 10 && AV_RL32(skip) == 1024); foundAudio = true;
        }
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !foundVideo) {
            CHECK(av_rescale_q(p->pts, st->time_base, (AVRational{1,30})) == videoStart);
            CHECK(av_rescale_q(p->dts, st->time_base, (AVRational{1,30})) == videoStart-reorder);
            CHECK(std::abs(av_q2d(st->time_base)*st->start_time - videoStart/30.0) < .001);
            foundVideo = true;
        }
        av_packet_unref(p.get());
    }
    CHECK(foundAudio && foundVideo); avformat_close_input(&input);
}
}

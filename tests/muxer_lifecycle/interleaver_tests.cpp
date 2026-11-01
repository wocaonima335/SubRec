#include "queue/packet_interleaver.h"

extern "C" {
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
}

#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__)); } while (false)

namespace {
constexpr AVRational kAudioTb{1, 48000};
constexpr AVRational kVideoTb{1, 30};

FFPacket *make(int64_t dts, int64_t duration, bool key)
{
    auto *packet = static_cast<FFPacket *>(av_mallocz(sizeof(FFPacket)));
    CHECK(packet);
    packet->packet.pts = packet->packet.dts = dts;
    packet->packet.duration = duration;
    packet->packet.flags = key ? AV_PKT_FLAG_KEY : 0;
    packet->packet.pos = -1;
    return packet;
}

void release(FFPacket *packet)
{
    av_packet_unref(&packet->packet);
    av_freep(&packet);
}

InterleaverConfig config(bool audio = true)
{
    InterleaverConfig c;
    c.audioEnabled = audio;
    c.audioTimeBase = kAudioTb;
    c.videoTimeBase = kVideoTb;
    return c;
}

struct Out {
    StreamKind kind;
    int64_t dts;
    int64_t duration;
    int64_t keyUs;
};

Out record(FFPacket *packet, StreamKind kind)
{
    const AVRational tb = kind == StreamKind::Audio ? kAudioTb : kVideoTb;
    Out out{kind, packet->packet.dts, packet->packet.duration,
            av_rescale_q(packet->packet.dts, tb, AVRational{1, 1000000})};
    release(packet);
    return out;
}

void drain(PacketInterleaver &il, std::vector<Out> &out, int64_t nowUs = 0)
{
    StreamKind kind;
    while (FFPacket *p = il.pop(nowUs, &kind))
        out.push_back(record(p, kind));
}

void randomOrderIsGloballySorted()
{
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> gap(1, 3), pick(0, 1);
    std::vector<int64_t> audio, video;
    for (int i = 0; i < 60000; ++i) audio.push_back(-1024 + 1024LL * i);   // AAC priming start
    int64_t tick = 3;                                                       // positive video start
    for (int i = 0; i < 40000; ++i) { video.push_back(tick); tick += gap(rng); }

    PacketInterleaver il(config());
    std::vector<Out> out;
    size_t ai = 0, vi = 0;
    while (ai < audio.size() || vi < video.size()) {
        const bool canAudio = ai < audio.size() && il.wants(StreamKind::Audio);
        const bool canVideo = vi < video.size() && il.wants(StreamKind::Video);
        CHECK(canAudio || canVideo);   // never deadlocks
        const bool takeAudio = canAudio && (!canVideo || pick(rng));
        if (takeAudio) il.push(StreamKind::Audio, make(audio[ai++], 1024, true));
        else { const bool key = vi == 0; il.push(StreamKind::Video, make(video[vi++], 1, key)); }
        if (ai == audio.size()) il.finish(StreamKind::Audio);
        if (vi == video.size()) il.finish(StreamKind::Video);
        drain(il, out);
    }
    StreamKind kind;
    while (FFPacket *p = il.popForFlush(&kind)) out.push_back(record(p, kind));
    CHECK(il.empty());
    CHECK(out.size() == audio.size() + video.size());

    int64_t lastKey = INT64_MIN, lastDts[2] = {INT64_MIN, INT64_MIN};
    std::vector<Out> perStream[2];
    for (const Out &o : out) {
        CHECK(o.keyUs >= lastKey);
        lastKey = o.keyUs;
        const int s = static_cast<int>(o.kind);
        CHECK(o.dts > lastDts[s]);
        lastDts[s] = o.dts;
        perStream[s].push_back(o);
    }
    for (int s = 0; s < 2; ++s) {
        for (size_t i = 0; i + 1 < perStream[s].size(); ++i)
            CHECK(perStream[s][i].duration == perStream[s][i + 1].dts - perStream[s][i].dts);
    }
    CHECK(il.stats().stallReleases == 0);
    CHECK(il.stats().maxPending[0] <= 64 && il.stats().maxPending[1] <= 64);
}

void durationsFollowTickGaps()
{
    PacketInterleaver il(config(false));
    std::vector<Out> out;
    for (int64_t t : {0, 1, 3, 6}) il.push(StreamKind::Video, make(t, 1, t == 0));
    il.finish(StreamKind::Video);
    drain(il, out);
    CHECK(out.size() == 4);
    CHECK(out[0].duration == 1 && out[1].duration == 2 && out[2].duration == 3);
    CHECK(out[3].duration == 1);   // last packet keeps the encoder value
    CHECK(il.stats().durationAdjusted[1] == 2);
}

void startupHoldsAudioWithoutStallRelease()
{
    PacketInterleaver il(config());
    std::vector<Out> out;
    int pushed = 0;
    while (il.wants(StreamKind::Audio)) il.push(StreamKind::Audio, make(-1024 + 1024LL * pushed++, 1024, true));
    CHECK(pushed == 24);
    drain(il, out, 0);
    drain(il, out, 10 * 1000000);   // no release during startup, however long
    CHECK(out.empty() && il.stats().stallReleases == 0);
    CHECK(il.stats().maxStartupPending[0] == 24);
    il.push(StreamKind::Video, make(0, 1, false));   // not a keyframe: dropped
    CHECK(il.stats().droppedBeforeKeyframe == 1 && !il.everPushed(StreamKind::Video));
    il.push(StreamKind::Video, make(0, 1, true));
    il.push(StreamKind::Video, make(1, 1, false));
    drain(il, out, 10 * 1000000);
    CHECK(!out.empty() && out.front().kind == StreamKind::Audio && out.front().dts == -1024);
    for (size_t i = 1; i < out.size(); ++i) CHECK(out[i].keyUs >= out[i - 1].keyUs);
}

void steadyStallReleasesOnce()
{
    PacketInterleaver il(config());
    std::vector<Out> out;
    int64_t a = 0, v = 0;
    auto pushAudio = [&] { il.push(StreamKind::Audio, make(a, 1024, true)); a += 1024; };
    auto pushVideo = [&] { il.push(StreamKind::Video, make(v, 1, v == 0)); ++v; };
    for (int i = 0; i < 30; ++i) { pushVideo(); pushAudio(); pushAudio(); drain(il, out, 0); }
    // Video stops; audio keeps arriving for 1.2 s.
    const size_t before = out.size();
    for (int64_t now = 0; now <= 1200000; now += 21333) { pushAudio(); drain(il, out, now); }
    CHECK(il.stats().stallReleases == 1);
    CHECK(out.size() > before + 30);
    // Video resumes: the stall flag clears and ordering resumes normally.
    v = 100;
    pushVideo(); pushVideo();
    drain(il, out, 1300000);
    CHECK(il.stats().stallReleases == 1);
}

void tiesGoToVideoAndAudioDisabled()
{
    PacketInterleaver il(config());
    std::vector<Out> out;
    il.push(StreamKind::Audio, make(0, 1600, true));
    il.push(StreamKind::Audio, make(1600, 1600, true));
    il.push(StreamKind::Video, make(0, 1, true));
    il.push(StreamKind::Video, make(1, 1, false));
    drain(il, out);
    CHECK(out.size() >= 2 && out[0].kind == StreamKind::Video && out[1].kind == StreamKind::Audio);

    PacketInterleaver videoOnly(config(false));
    CHECK(!videoOnly.wants(StreamKind::Audio));
    videoOnly.push(StreamKind::Audio, make(0, 1024, true));   // released, not queued
    CHECK(videoOnly.pending(StreamKind::Audio) == 0);
    videoOnly.push(StreamKind::Video, make(0, 1, true));
    videoOnly.push(StreamKind::Video, make(1, 1, false));
    std::vector<Out> vo;
    drain(videoOnly, vo);
    CHECK(vo.size() == 1);
}

void destructorReleasesPending()
{
    PacketInterleaver il(config());
    for (int i = 0; i < 10; ++i) il.push(StreamKind::Audio, make(1024LL * i, 1024, true));
    il.push(StreamKind::Video, make(0, 1, true));
    CHECK(il.pending(StreamKind::Audio) == 10 && il.pending(StreamKind::Video) == 1);
    // Destructor frees them; run under ASan to catch double frees.
}
}

int main()
{
    try {
        randomOrderIsGloballySorted();
        durationsFollowTickGaps();
        startupHoldsAudioWithoutStallRelease();
        steadyStallReleasesOnce();
        tiesGoToVideoAndAudioDisabled();
        destructorReleasesPending();
        std::cout << "PASS: packet interleaver\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

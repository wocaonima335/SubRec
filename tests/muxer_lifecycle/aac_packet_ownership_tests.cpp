#include "encoder/ffaencoder.h"
#include "queue/ffapacketqueue.h"
#include <iostream>
#include <stdexcept>

// Instrument only the production encoder's packet-shell API calls. FFmpeg and
// the real queue keep their normal implementation; no production test hooks.
static int outstandingPackets = 0;
static int allocatedPackets = 0;
static AVPacket *trackedPacketAlloc() {
    auto *packet = av_packet_alloc();
    if (packet) { ++outstandingPackets; ++allocatedPackets; }
    return packet;
}
static void trackedPacketFree(AVPacket **packet) {
    if (*packet) --outstandingPackets;
    av_packet_free(packet);
}
#define av_packet_alloc trackedPacketAlloc
#define av_packet_free trackedPacketFree
#include "../../encoder/ffaencoder.cpp"
#undef av_packet_alloc
#undef av_packet_free
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

int main()
{
    try {
        FFAPacketQueue queue; queue.start();
        FFAEncoder encoder; encoder.init(&queue);
        AVFrame *frame = av_frame_alloc(); CHECK(frame);
        struct Cleanup { AVFrame *&frame; ~Cleanup() { av_frame_free(&frame); } } cleanup{frame};
        frame->format = AV_SAMPLE_FMT_FLTP; frame->sample_rate = 48000; frame->nb_samples = 1024;
        av_channel_layout_default(&frame->ch_layout, 2);
        CHECK(av_frame_get_buffer(frame, 0) == 0);
        av_samples_set_silence(frame->extended_data, 0, 1024, 2, AV_SAMPLE_FMT_FLTP);
        encoder.initAudio(frame); CHECK(encoder.getCodecCtx());
        int packetCount = 0;
        auto drain = [&] {
            while (true) {
                auto packet = queue.tryReceive();
                if (!packet.ok()) break;
                ++packetCount;
                FFPacketTraits::release(packet.item);
            }
        };
        for (int n=0; n<2000; ++n) {
            CHECK(encoder.encode(frame, 0, n*1024LL, {1,48000}) == 0);
            drain();
            CHECK(outstandingPackets == 0);
        }
        const int beforeFlush = packetCount;
        CHECK(encoder.finish(0) == 0); drain();
        CHECK(packetCount > beforeFlush && packetCount == 2001);
        CHECK(allocatedPackets > 2001 && outstandingPackets == 0);
        encoder.close();
        std::cout << "PASS AAC packet shells released after encode and flush: " << packetCount << " packets\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

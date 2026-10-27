#pragma once
#include "queue/ffapacketqueue.h"
#include <cstdlib>
// This standalone test passes nullptr as the recorder. UI/error fan-out must
// never execute; codec, packet queues, worker and muxer are production classes.
class FFRecorder {
public:
    FFAPacketQueue *getAFilterEncoderFrmQueue() { std::abort(); }
    FFAPacketQueue *getVFilterEncoderFrmQueue() { std::abort(); }
    FFAPacketQueue *getADecoderFrmQueue(int) { std::abort(); }
    FFAPacketQueue *getADecoderPktQueue(int) { std::abort(); }
    FFAPacketQueue *getVDecoderFrmQueue(int) { std::abort(); }
    FFAPacketQueue *getVDecoderPktQueue(int) { std::abort(); }
};

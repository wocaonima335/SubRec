#pragma once
#include "recorder/ffrecorder.h"
class FFCloseSourceEvent {
public:
    FFCloseSourceEvent(FFRecorder *, FFRecordContextType::demuxerType) {}
    void work() { std::abort(); } // Failed startup must use the abort path.
    bool completed() const { return false; }
};

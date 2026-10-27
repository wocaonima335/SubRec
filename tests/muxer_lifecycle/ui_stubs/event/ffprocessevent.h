#pragma once
#include <cstdlib>
class FFRecorder;
class FFCaptureProcessEvent {
public:
    FFCaptureProcessEvent(FFRecorder *, double) { std::abort(); }
    void work() { std::abort(); }
};

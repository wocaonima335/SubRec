#pragma once
// Device-boundary fakes for the REAL CaptureSession cancellation code. Each
// producer uses a real OS thread blocked on a real bounded frame queue. close()
// models closing the DirectShow graph (stopping its device callbacks).
#include "capture/capture_types.h"
#include "queue/ffvframequeue.h"
#include "thread/ffscreengrabthread.h"
#include "thread/stop_deadline.h"
#include <atomic>
#include <cassert>
#include <memory>
#include <vector>
#include "queue/recordersyncqueue.h"

namespace FFRecordContextType {
enum demuxerType { AUDIO, MICROPHONE, SCREEN, CAMERA };
constexpr size_t A_DECODER_SIZE = 2, V_DECODER_SIZE = 2;
}

class FakeDeviceWorker : public FFThread {
public:
    FFVFrameQueue queue;
    std::atomic<bool> entered{false}, exited{false}, closed{false};
    static inline std::atomic<int> live{0};
    bool stuck = false;
    void run() override {
        ++live;
        entered = true;
        AVFrame *frame = av_frame_alloc();
        while (stuck) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        while (!m_stop.load()) queue.enqueue(frame);
        av_frame_free(&frame);
        exited = true;
        --live;
    }
    void requestAbort() { stop(); queue.close(); }
    void requestDrainStop(std::chrono::milliseconds = {}) { requestAbort(); }
    void wakeAllThread() { requestAbort(); }
    void finishQueuedPackets() { requestAbort(); }
    void close() { assert(exited || !entered); closed = true; }
    int lastError() const { return 0; }
    void stopEncoder() {}
    void closeAudioSource(int) { close(); }
    void closeVideoSource(int) {}
    bool joinFor(std::chrono::milliseconds limit) { return waitFor(limit); }
    void abandon() { std::abort(); } // Fake device cancellation MUST join.
};
using FFMuxerThread = FakeDeviceWorker;
struct FakePipeline {
    FakeDeviceWorker *aDemuxerThread[2]{}, *aDecoderThread[2]{};
    FakeDeviceWorker *vDemuxerThread[2]{}, *vDecoderThread[2]{};
    FFVFrameQueue *aDecoderPktQueue[2]{}, *aDecoderFrmQueue[2]{};
    FFVFrameQueue *vDecoderPktQueue[2]{}, *vDecoderFrmQueue[2]{};
    FFVFrameQueue *aFilterEncoderFrmQueue = nullptr, *vFilterEncoderFrmQueue = nullptr;
    FFVFrameQueue *aEncoderPktQueue = nullptr, *vEncoderPktQueue = nullptr;
    FakeDeviceWorker *aFilterThread = nullptr, *vFilterThread = nullptr;
    FakeDeviceWorker *aEncoderThread = nullptr, *vEncoderThread = nullptr;
    FakeDeviceWorker *muxerThread = nullptr;
    FFScreenGrabThread *screenGrabThread = nullptr;
    std::unique_ptr<FakeDeviceWorker> systemAudioCapture;
};
class FFRecorder {
public:
    FakePipeline pipeline;
    std::atomic<bool> recording{false};
    std::vector<std::unique_ptr<FakeDeviceWorker>> audio;
    FFScreenGrabThread screen;
    FFVFrameQueue videoQueue;
    FFRecorder() {
        // system audio, mic demux/decoder, filter and encoder all block upstream
        // of video startup; the real session must cancel all of them.
        for (int i = 0; i < 6; ++i) audio.emplace_back(new FakeDeviceWorker);
        pipeline.aDemuxerThread[0] = audio[0].get();
        pipeline.aDemuxerThread[1] = audio[1].get();
        pipeline.aDecoderThread[0] = audio[2].get();
        pipeline.aDecoderThread[1] = audio[3].get();
        pipeline.aFilterThread = audio[4].get();
        pipeline.aEncoderThread = audio[5].get();
        pipeline.screenGrabThread = &screen;
        pipeline.vDecoderFrmQueue[0] = &videoQueue;
    }
    void setCaptureError(const QString &, const QString &) {}
    RecorderSyncQueue *getSyncQueue() { return nullptr; }
    void initialize() {}
    void startRecord(const CaptureConfig &) { recording = true; }
    void stopRecord() { recording = false; }
    bool isRecording() const { return recording; }
    FakePipeline *getContext() { return &pipeline; }
    FakeDeviceWorker *getMuxerThread() { return nullptr; }
};

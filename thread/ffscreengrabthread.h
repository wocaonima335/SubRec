#ifndef FFSCREENGRABTHREAD_H
#define FFSCREENGRABTHREAD_H

#include "ffthread.h"

#include "capture/capture_types.h"
#include "capture/wasapi_worker_lifetime.h"

#include <condition_variable>
#include <functional>
#include <mutex>

class FFVFrameQueue;
class QObject;

// Screen frame producer for the ddagrab backend. It replaces the screen demuxer + decoder
// pair: frames go straight into the decoder frame queue that FFVFilterThread reads
// (vDecoderFrmQueue[SCREEN]), so the filter/encoder/muxer chain is unchanged.
// Lifecycle mirrors FFDemuxerThread's prepared screen open:
//   prepareOpenAndWait() -> resume() -> requestDrainStop()/requestAbort() -> wait() -> close().
class FFScreenGrabThread : public FFThread
{
public:
    FFScreenGrabThread();
    ~FFScreenGrabThread() override;

    // Opens the ddagrab graph on the grab thread (DXGI duplication is created and used on
    // the same thread) and waits for the result. On success the thread idles until resume().
    // recorder (may be null) receives setCaptureError() if the duplication is lost later.
    CaptureStartResult prepareOpenAndWait(const InputRequest &request,
                                          FFVFrameQueue *queue,
                                          QObject *recorder,
                                          int timeoutMs,
                                          const std::function<bool()> &shouldOpen);
    void resume();
    // Stop grabbing and send finish (EOF) to the queue, so filter/encoder drain normally.
    void requestDrainStop();
    // Stop grabbing and close the queue (failed start / abort path).
    void requestAbort();
    void wakeAllThread();
    // After wait(): release the graph and forget the session.
    void close();

    // True between a successful prepareOpenAndWait() and close().
    bool isActive() const;
    bool gpuFrames() const;
    int width() const;
    int height() const;
    QString describe() const;

    void stop() override { requestAbort(); }
    void wait() override;
    bool waitFor(std::chrono::milliseconds timeout) override;

private:
    // Only cancelled workers may detach: their state and queue storage are owned.
    void abandon();
    using FFThread::start; // Only prepareOpenAndWait may launch this worker.
    void run() override {} // This adapter uses WorkerLifetime, not FFThread::start().
    struct State;
    std::shared_ptr<State> state_;
    std::unique_ptr<capture::wasapi::WorkerLifetime> lifetime_;
};

#endif // FFSCREENGRABTHREAD_H

#ifndef FFSCREENGRABTHREAD_H
#define FFSCREENGRABTHREAD_H

#include "ffthread.h"

#include "capture/capture_types.h"
#include "capture/ddagrab_screen_source.h"

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

protected:
    void run() override;

private:
    void grabLoop();
    bool reopenAfterLoss();
    void reportLoss(const QString &reason);

    DdagrabScreenSource m_source;
    InputRequest m_request;
    FFVFrameQueue *m_queue = nullptr;
    QObject *m_recorder = nullptr;
    std::function<bool()> m_openGuard;

    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
    bool m_openDone = false;
    bool m_resumeRequested = false;
    bool m_active = false;
    bool m_abortRequested = false;
    CaptureStartResult m_openResult;
    bool m_gpuFrames = false;
    int m_width = 0;
    int m_height = 0;
    QString m_description;
};

#endif // FFSCREENGRABTHREAD_H

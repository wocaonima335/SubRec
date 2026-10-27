#include "ffclosesourceevent.h"

#include "thread/ffscreengrabthread.h"
#include "thread/ffmuxerthread.h"

FFCloseSourceEvent::FFCloseSourceEvent(FFRecorder *recordCtx, demuxerType sourceType_)
    : FFEvent(recordCtx)
{
    index = FFRecordContextType::demuxerIndex[sourceType_];
    sourceType = sourceType_;
}

bool FFCloseSourceEvent::waitForWorker(FFThread *worker,
                                     const std::function<void()> &cancel, const char *name)
{
    const bool joined = capture_stop::drain(*worker, cancel, [name](const char *message) {
        qWarning() << "[FFCloseSourceEvent]" << name << message;
    }, stopTimeout);
    completed_ = completed_ && joined;
    return joined;
}

void FFCloseSourceEvent::work()
{
    // Arm before waiting on any upstream stage: a missing opposite stream must
    // not leave encoder producers blocked behind the muxer during shutdown.
    if (muxerThread) muxerThread->requestDrainStop(stopTimeout);
    close();
}

void FFCloseSourceEvent::close()
{
    using namespace FFRecordContextType;
    qDebug() << "[FFCloseSourceEvent] close begin, sourceType=" << sourceType
             << "index=" << index;

    if (sourceType == AUDIO || sourceType == MICROPHONE) {
        closeAudioSourceChain();
    } else {
        closeVideoSourceChain();
    }

    qDebug() << "[FFCloseSourceEvent] close end";
}

void FFCloseSourceEvent::closeAudioSourceChain()
{
    stopAudioInputPath();
    if (!completed_) return;
    if (sourceType == AUDIO && aFilterThread && aFilterThread->isMixing()) {
        FFCloseSourceEvent microphone(recoderContext, MICROPHONE);
        microphone.stopAudioInputPath();
        if (!microphone.completed()) { completed_ = false; return; }
    }
    drainAudioEncoderPath();
}

void FFCloseSourceEvent::closeVideoSourceChain()
{
    stopVideoInputPath();
    if (!completed_) return;
    drainVideoEncoderPath();
}

void FFCloseSourceEvent::drainAudioEncoderPath()
{
    if (aFilterThread) {
        qDebug() << "[FFCloseSourceEvent] audio filter wait";
        if (!waitForWorker(aFilterThread, [&] {
            aFilterThread->stop();
            if (aFilterEncoderFrmQueue) aFilterEncoderFrmQueue->sendFinish();
            aFilterThread->wakeAllThread();
        }, "audio filter")) return;
        aFilterThread->stopEncoder();
        aFilterThread->closeAudioSource(sourceType);
        aFilterThread->stop();
    }
    qDebug() << "[FFCloseSourceEvent] audio filter source closed";

    if (aFilter) {
        aFilter->sendFinish();
        qDebug() << "[FFCloseSourceEvent] audio filter finish ensured";
    }

    if (aEncoderThread) {
        qDebug() << "[FFCloseSourceEvent] audio encoder drain";
        aEncoderThread->requestDrainStop();
        if (!waitForWorker(aEncoderThread, [&] {
            aEncoderThread->stop();
            if (aFilterEncoderFrmQueue) aFilterEncoderFrmQueue->close();
            if (aEncoderPktQueue) aEncoderPktQueue->sendFinish();
        }, "audio encoder")) return;
        qDebug() << "[FFCloseSourceEvent] audio encoder close";
        aEncoderThread->close();
    }
    qDebug() << "[FFCloseSourceEvent] audio encoder thread closed";

    if (aEncoderPktQueue) {
        aEncoderPktQueue->sendFinish();
        qDebug() << "[FFCloseSourceEvent] audio encoder packet queue finish ensured";
    }
}

void FFCloseSourceEvent::drainVideoEncoderPath()
{
    if (vFilterThread) {
        qDebug() << "[FFCloseSourceEvent] video filter wait";
        if (!waitForWorker(vFilterThread, [&] {
            vFilterThread->stop();
            if (vFilterEncoderFrmQueue) vFilterEncoderFrmQueue->sendFinish();
            vFilterThread->wakeAllThread();
        }, "video filter")) return;
        vFilterThread->stopEncoder();
        vFilterThread->closeVideoSource(sourceType);
        vFilterThread->stop();
    }
    qDebug() << "[FFCloseSourceEvent] video filter source closed";

    if (vFilter) {
        vFilter->sendFinish();
        qDebug() << "[FFCloseSourceEvent] video filter finish ensured";
    }

    if (vEncoderThread) {
        qDebug() << "[FFCloseSourceEvent] video encoder drain";
        vEncoderThread->requestDrainStop();
        if (!waitForWorker(vEncoderThread, [&] {
            vEncoderThread->stop();
            if (vFilterEncoderFrmQueue) vFilterEncoderFrmQueue->close();
            if (vEncoderPktQueue) vEncoderPktQueue->sendFinish();
        }, "video encoder")) return;
        qDebug() << "[FFCloseSourceEvent] video encoder close";
        vEncoderThread->close();
    }
    qDebug() << "[FFCloseSourceEvent] video encoder thread closed";

    if (vEncoderPktQueue) {
        vEncoderPktQueue->sendFinish();
        qDebug() << "[FFCloseSourceEvent] video encoder packet queue finish ensured";
    }
}

void FFCloseSourceEvent::stopAudioInputPath()
{
#ifdef _WIN32
    auto &capture = recoderContext->getContext()->systemAudioCapture;
    if (sourceType == AUDIO && capture && capture->state() != capture::wasapi::LoopbackState::Idle) {
        capture::wasapi::stopWithDeadline(*capture, [&] {
            // Release blocked producer/consumer waits when normal EOF cannot arrive.
            for (auto *queue : aPktQueue) if (queue) queue->close();
            for (auto *queue : aFrmQueue) if (queue) queue->close();
            if (aFilterEncoderFrmQueue) aFilterEncoderFrmQueue->close();
            if (aEncoderPktQueue) aEncoderPktQueue->close();
        }, [](const char *message) { qWarning() << "[WASAPI] stop:" << message; });
        return; // This source owns EOF; idle decoders must not send it again.
    }
#endif
    if (aDemuxerThread[index]) {
        qDebug() << "[FFCloseSourceEvent] audio demuxer drain";
        aDemuxerThread[index]->requestDrainStop();
        qDebug() << "[FFCloseSourceEvent] audio demuxer wait";
        if (!waitForWorker(aDemuxerThread[index], [&] {
            aDemuxerThread[index]->requestAbort();
        }, "audio demuxer")) return;
        qDebug() << "[FFCloseSourceEvent] audio demuxer close";
        aDemuxerThread[index]->close();
    }
    qDebug() << "[FFCloseSourceEvent] audio demuxer thread closed";

    if (aDecoderThread[index]) {
        qDebug() << "[FFCloseSourceEvent] audio decoder drain";
        aDecoderThread[index]->requestDrainStop();
        qDebug() << "[FFCloseSourceEvent] audio decoder wait";
        if (!waitForWorker(aDecoderThread[index], [&] {
            if (aFrmQueue[index]) aFrmQueue[index]->close();
            aDecoderThread[index]->requestAbort();
        }, "audio decoder")) return;
        qDebug() << "[FFCloseSourceEvent] audio decoder close";
        aDecoderThread[index]->close();
    }
    qDebug() << "[FFCloseSourceEvent] audio decoder thread closed";
}

void FFCloseSourceEvent::stopVideoInputPath()
{
    // ddagrab screen: the grab thread is the producer of vDecoderFrmQueue[SCREEN]; stopping
    // it sends EOF there. The (idle) screen demuxer/decoder below are then no-ops.
    if (sourceType == SCREEN && screenGrabThread && screenGrabThread->isActive()) {
        qDebug() << "[FFCloseSourceEvent] screen grab drain";
        screenGrabThread->requestDrainStop();
        if (!waitForWorker(screenGrabThread, [&] {
            screenGrabThread->requestAbort();
        }, "screen grab")) return;
        screenGrabThread->close();
        qDebug() << "[FFCloseSourceEvent] screen grab thread closed";
    }

    if (vDemuxerThread[index]) {
        qDebug() << "[FFCloseSourceEvent] video demuxer drain";
        vDemuxerThread[index]->requestDrainStop();
        qDebug() << "[FFCloseSourceEvent] video demuxer wait";
        if (!waitForWorker(vDemuxerThread[index], [&] {
            vDemuxerThread[index]->requestAbort();
        }, "video demuxer")) return;
        qDebug() << "[FFCloseSourceEvent] video demuxer close";
        vDemuxerThread[index]->close();
    }
    qDebug() << "[FFCloseSourceEvent] video demuxer thread closed";

    if (vDecoderThread[index]) {
        qDebug() << "[FFCloseSourceEvent] video decoder drain";
        vDecoderThread[index]->requestDrainStop();
        qDebug() << "[FFCloseSourceEvent] video decoder wait";
        if (!waitForWorker(vDecoderThread[index], [&] {
            if (vFrmQueue[index]) vFrmQueue[index]->close();
            vDecoderThread[index]->requestAbort();
        }, "video decoder")) return;
        qDebug() << "[FFCloseSourceEvent] video decoder close";
        vDecoderThread[index]->close();
    }
    qDebug() << "[FFCloseSourceEvent] video decoder thread closed";
}

#include "ffclosesourceevent.h"

#include "thread/ffscreengrabthread.h"

FFCloseSourceEvent::FFCloseSourceEvent(FFRecorder *recordCtx, demuxerType sourceType_)
    : FFEvent(recordCtx)
{
    index = FFRecordContextType::demuxerIndex[sourceType_];
    sourceType = sourceType_;
}

void FFCloseSourceEvent::work()
{
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
    drainAudioEncoderPath();
}

void FFCloseSourceEvent::closeVideoSourceChain()
{
    stopVideoInputPath();
    drainVideoEncoderPath();
}

void FFCloseSourceEvent::drainAudioEncoderPath()
{
    if (aFilterThread) {
        qDebug() << "[FFCloseSourceEvent] audio filter wait";
        aFilterThread->wait();
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
        aEncoderThread->wait();
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
        vFilterThread->wait();
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
        vEncoderThread->wait();
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
        capture->requestDrainStop();
        capture->join();
        return; // This source owns EOF; idle decoders must not send it again.
    }
#endif
    if (aDemuxerThread[index]) {
        qDebug() << "[FFCloseSourceEvent] audio demuxer drain";
        aDemuxerThread[index]->requestDrainStop();
        qDebug() << "[FFCloseSourceEvent] audio demuxer wait";
        aDemuxerThread[index]->wait();
        qDebug() << "[FFCloseSourceEvent] audio demuxer close";
        aDemuxerThread[index]->close();
    }
    qDebug() << "[FFCloseSourceEvent] audio demuxer thread closed";

    if (aDecoderThread[index]) {
        qDebug() << "[FFCloseSourceEvent] audio decoder drain";
        aDecoderThread[index]->requestDrainStop();
        qDebug() << "[FFCloseSourceEvent] audio decoder wait";
        aDecoderThread[index]->wait();
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
        screenGrabThread->wait();
        screenGrabThread->close();
        qDebug() << "[FFCloseSourceEvent] screen grab thread closed";
    }

    if (vDemuxerThread[index]) {
        qDebug() << "[FFCloseSourceEvent] video demuxer drain";
        vDemuxerThread[index]->requestDrainStop();
        qDebug() << "[FFCloseSourceEvent] video demuxer wait";
        vDemuxerThread[index]->wait();
        qDebug() << "[FFCloseSourceEvent] video demuxer close";
        vDemuxerThread[index]->close();
    }
    qDebug() << "[FFCloseSourceEvent] video demuxer thread closed";

    if (vDecoderThread[index]) {
        qDebug() << "[FFCloseSourceEvent] video decoder drain";
        vDecoderThread[index]->requestDrainStop();
        qDebug() << "[FFCloseSourceEvent] video decoder wait";
        vDecoderThread[index]->wait();
        qDebug() << "[FFCloseSourceEvent] video decoder close";
        vDecoderThread[index]->close();
    }
    qDebug() << "[FFCloseSourceEvent] video decoder thread closed";
}

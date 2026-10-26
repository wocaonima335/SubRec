#include "ffrecorder_p.h"

using namespace FFRecordContextType;
FFRecorderPrivate::FFRecorderPrivate()
{
    for (size_t i = 0; i < FFRecordContextType::A_DECODER_SIZE; ++i) {
        aDecoderThread[i] = nullptr;
        aDecoderPktQueue[i] = nullptr;
        aDecoderFrmQueue[i] = nullptr;
        aDecoder[i] = nullptr;
    }
    for (size_t i = 0; i < FFRecordContextType::V_DECODER_SIZE; ++i) {
        vDecoderThread[i] = nullptr;
        vDecoderPktQueue[i] = nullptr;
        vDecoderFrmQueue[i] = nullptr;
        vDecoder[i] = nullptr;
    }

    for (size_t i = 0; i < FFRecordContextType::A_DEMUXER_SIZE; ++i) {
        aDemuxerThread[i] = nullptr;
        aDemuxer[i] = nullptr;
    }

    for (size_t i = 0; i < FFRecordContextType::V_DEMUXER_SIZE; ++i) {
        vDemuxerThread[i] = nullptr;
        vDemuxer[i] = nullptr;
    }

    vEncoderPktQueue = nullptr;
    aEncoderPktQueue = nullptr;
    vFilterEncoderFrmQueue = nullptr;
    aFilterEncoderFrmQueue = nullptr;
    syncQueue = nullptr;

    muxer = nullptr;
    muxerThread = nullptr;
    aEncoder = nullptr;
    aEncoderThread = nullptr;
    vEncoder = nullptr;
    vEncoderThread = nullptr;
    vFilter = nullptr;
    vFilterThread = nullptr;
    screenGrabThread = nullptr;
    aFilter = nullptr;
    aFilterThread = nullptr;

    audioSampler = nullptr;
}

FFRecorderPrivate::~FFRecorderPrivate()
{
    auto stopAndDeleteThread = [](auto *&thread) {
        if (thread) {
            thread->stop();
            thread->wakeAllThread();
            thread->wait();
            delete thread;
            thread = nullptr;
        }
    };

#ifdef _WIN32
    if (systemAudioCapture) {
        systemAudioCapture->requestAbort();
        if (aDecoderFrmQueue[A_AUDIO]) aDecoderFrmQueue[A_AUDIO]->close();
        systemAudioCapture->join();
        systemAudioCapture.reset();
        systemAudioSink.reset();
    }
#endif
    stopAndDeleteThread(screenGrabThread);
    stopAndDeleteThread(muxerThread);
    stopAndDeleteThread(aEncoderThread);
    stopAndDeleteThread(vEncoderThread);
    stopAndDeleteThread(vFilterThread);
    stopAndDeleteThread(aFilterThread);

    for (size_t i = 0; i < FFRecordContextType::A_DECODER_SIZE; ++i) {
        stopAndDeleteThread(aDecoderThread[i]);
        stopAndDeleteThread(aDemuxerThread[i]);
        delete aDecoderPktQueue[i];
        delete aDecoderFrmQueue[i];
        delete aDecoder[i];
        delete aDemuxer[i];
    }

    for (size_t i = 0; i < FFRecordContextType::V_DECODER_SIZE; ++i) {
        stopAndDeleteThread(vDecoderThread[i]);
        stopAndDeleteThread(vDemuxerThread[i]);
        delete vDecoderPktQueue[i];
        delete vDecoderFrmQueue[i];
        delete vDecoder[i];
        delete vDemuxer[i];
    }

    delete muxer;
    delete aEncoder;
    delete vEncoder;
    delete vFilter;
    delete aFilter;
    delete vEncoderPktQueue;
    delete aEncoderPktQueue;
    delete vFilterEncoderFrmQueue;
    delete aFilterEncoderFrmQueue;
    delete syncQueue;
    delete audioSampler;
}

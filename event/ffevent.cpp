#include "ffevent.h"

FFEvent::FFEvent(FFRecorder *recoderContext_)
{
    recoderContext = recoderContext_;

    if (!recoderContext)
        return;

    for (size_t i = 0; i < FFRecordContextType::A_DECODER_SIZE; i++) {
        aPktQueue[i] = this->recoderContext->getADecoderPktQueue(i);
        aFrmQueue[i] = this->recoderContext->getADecoderFrmQueue(i);
        aDecoder[i] = this->recoderContext->getADecoder(i);
        aDemuxer[i] = this->recoderContext->getADemuxer(i);
        aDemuxerThread[i] = this->recoderContext->getADemuxerThread(i);
        aDecoderThread[i] = this->recoderContext->getADecoderThread(i);
    }

    for (size_t i = 0; i < FFRecordContextType::V_DECODER_SIZE; i++) {
        vPktQueue[i] = this->recoderContext->getVDecoderPktQueue(i);
        vFrmQueue[i] = this->recoderContext->getVDecoderFrmQueue(i);
        vDecoder[i] = this->recoderContext->getVDecoder(i);
        vDemuxer[i] = this->recoderContext->getVDemuxer(i);
        vDemuxerThread[i] = this->recoderContext->getVDemuxerThread(i);
        vDecoderThread[i] = this->recoderContext->getVDecoderThread(i);
    }

    aFilter = this->recoderContext->getAFilter();
    vFilter = this->recoderContext->getVFilter();

    aFilterThread = this->recoderContext->getAFilterThread();
    vFilterThread = this->recoderContext->getVFilterThread();

    aFilterEncoderFrmQueue = this->recoderContext->getAFilterEncoderFrmQueue();
    vFilterEncoderFrmQueue = this->recoderContext->getVFilterEncoderFrmQueue();

    aEncoder = this->recoderContext->getAEncoder();
    vEncoder = this->recoderContext->getVEncoder();

    aEncoderPktQueue = this->recoderContext->getAEncoderPktQueue();
    vEncoderPktQueue = this->recoderContext->getVEncoderPktQueue();

    aEncoderThread = this->recoderContext->getAEncoderThread();
    vEncoderThread = this->recoderContext->getVEncoderThread();

    muxer = this->recoderContext->getMuxer();
    muxerThread = this->recoderContext->getMuxerThread();
}

FFEvent::~FFEvent() {}

#include "ffaencoderthread.h"

#include "encoder/ffaencoder.h"
#include "muxer/ffmuxer.h"
#include "queue/ffaframequeue.h"
#include "queue/recordersyncqueue.h"

#include <QDebug>
#include <algorithm>

FFAEncoderThread::FFAEncoderThread() {}

FFAEncoderThread::~FFAEncoderThread() {}

void FFAEncoderThread::init(FFAFilter *aFilter_,
                            FFAEncoder *aEncoder_,
                            FFMuxer *muxer_,
                            FFAFrameQueue *frmQueue_,
                            RecorderSyncQueue *syncQueue_)
{
    aEncoder = aEncoder_;
    muxer = muxer_;
    frmQueue = frmQueue_;
    aFilter = aFilter_;
    syncQueue = syncQueue_;
}

void FFAEncoderThread::close()
{
    if (aEncoder)
        aEncoder->close();
    streamIndex = -1;
    last_apts = AV_NOPTS_VALUE;
    next_apts = AV_NOPTS_VALUE;
    paused.store(false, std::memory_order_release);
    pause_start_us = 0;
    pause_accum_us = 0;
}

void FFAEncoderThread::wakeAllThread()
{
    requestAbort();
}

void FFAEncoderThread::requestDrainStop()
{
    qDebug() << "[AEncThread] requestDrainStop";
    if (aEncoder) {
        aEncoder->requestDrainStop();
    }
}

void FFAEncoderThread::requestAbort()
{
    m_stop.store(true, std::memory_order_release);
    if (frmQueue) {
        frmQueue->close(MediaQueueCloseReason::RequestedStop);
    }
    if (aEncoder) {
        aEncoder->requestAbort();
    }
}

void FFAEncoderThread::onPauseChanged(bool pauseFlag, int64_t ts_us)
{
    std::unique_lock<std::mutex> lk(pause_mutex);
    int64_t now = ts_us > 0 ? ts_us : av_gettime_relative();

    if (pauseFlag) {
        if (paused.load(std::memory_order_relaxed)) {
            return;
        }
        paused.store(true, std::memory_order_release);
        pause_cv.notify_all();
        pause_start_us = now;
        qDebug() << "[AEncThread] paused at" << pause_start_us << "us";
    } else {
        if (!paused.load(std::memory_order_relaxed)) {
            return;
        }
        paused.store(false, std::memory_order_release);
        pause_cv.notify_all();
        qDebug() << "[AEncThread] resumed at" << now << "us";
    }
}

void FFAEncoderThread::run()
{
    while (!m_stop) {
        auto received = frmQueue->receive();
        if (received.result == MediaQueueResult::Eof) {
            if (syncQueue) {
                syncQueue->finishAudio();
                drainReadyAudioFrames();
            }
            if (aEncoder) {
                qDebug() << "[AEncThread] frame queue finished, draining encoder";
                aEncoder->finish(streamIndex);
            }
            break;
        }
        if (received.result != MediaQueueResult::Ok || received.item == nullptr) {
            qDebug() << "[AEncThread] receive exit result="
                     << mediaQueueResultName(received.result);
            break;
        }
        AVFrame *frame = received.item;
        if (AVFrameTraits::isNull(frame)) {
            AVFrameTraits::release(frame);
            break;
        }

        if (streamIndex == -1) {
            initEncoder(frame);
        }

        if (paused.load(std::memory_order_acquire)) {
            AVFrameTraits::release(frame);
            continue;
        }

        if (syncQueue) {
            const auto pushResult = syncQueue->pushAudioFrame(frame);
            AVFrameTraits::release(frame);
            if (pushResult == MediaQueueResult::Error) {
                qWarning() << "[AEncThread] sync queue push audio failed";
                break;
            }
            drainReadyAudioFrames();
            continue;
        }

        int64_t apts = next_apts == AV_NOPTS_VALUE ? 0 : next_apts;
        AVRational sampleTimeBase{1, frame->sample_rate > 0 ? frame->sample_rate : 1};
        int64_t step = av_rescale_q(frame->nb_samples, sampleTimeBase, audioTimeBase);
        if (step <= 0) {
            step = std::max<int64_t>(1, frame->nb_samples);
        }
        last_apts = apts;
        next_apts = apts + step;
        aEncoder->encode(frame, streamIndex, apts, audioTimeBase);
        AVFrameTraits::release(frame);
    }
}

void FFAEncoderThread::initEncoder(AVFrame *frame)
{
    if (!aEncoder || !muxer) {
        qWarning() << "[AEncThread] initEncoder failed: null encoder or muxer";
        return;
    }

    aEncoder->initAudio(frame);
    audioTimeBase = aEncoder->getCodecCtx()->time_base;
    muxer->addStream(aEncoder->getCodecCtx());
    streamIndex = muxer->getAStreamIndex();

    if (streamIndex < 0) {
        qWarning() << "[AEncThread] initEncoder failed: invalid stream index" << streamIndex;
    } else {
        qDebug() << "[AEncThread] initialized: streamIndex=" << streamIndex
                 << "timeBase=" << audioTimeBase.num << "/" << audioTimeBase.den;
    }

    if (syncQueue && aEncoder && aEncoder->getCodecCtx()) {
        syncQueue->configureAudio(aEncoder->getCodecCtx()->sample_fmt,
                                  aEncoder->getCodecCtx()->sample_rate,
                                  aEncoder->getCodecCtx()->ch_layout,
                                  std::max(1, aEncoder->getCodecCtx()->frame_size),
                                  audioTimeBase);
    }
}

void FFAEncoderThread::drainReadyAudioFrames()
{
    if (!syncQueue || !aEncoder || streamIndex < 0) {
        return;
    }

    while (true) {
        auto ready = syncQueue->takeReadyAudioFrame();
        if (ready.result == MediaQueueResult::Again
            || ready.result == MediaQueueResult::Eof) {
            return;
        }
        if (ready.result != MediaQueueResult::Ok || ready.item == nullptr) {
            qWarning() << "[AEncThread] sync queue receive exit result="
                       << mediaQueueResultName(ready.result);
            return;
        }

        AVFrame *readyFrame = ready.item;
        last_apts = readyFrame->pts;
        next_apts = readyFrame->pts + readyFrame->nb_samples;
        aEncoder->encode(readyFrame, streamIndex, readyFrame->pts, audioTimeBase);
        AVFrameTraits::release(readyFrame);
    }
}

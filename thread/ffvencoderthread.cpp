#include "ffvencoderthread.h"

#include "encoder/ffvencoder.h"
#include "filter/ffvfilter.h"
#include "muxer/ffmuxer.h"
#include "opengl/ffglitem.h"
#include "queue/recordersyncqueue.h"
#include "queue/ffvframequeue.h"
#include "recorder/ffrecorder.h"

#include <QMetaObject>
#include <QString>

extern "C" {
#include <libavutil/pixfmt.h>
}

FFVEncoderThread::FFVEncoderThread() {}

FFVEncoderThread::~FFVEncoderThread() {}

void FFVEncoderThread::init(FFVFilter *vFilter_,
                            FFVEncoder *vEncoder_,
                            FFMuxer *muxer_,
                            FFVFrameQueue *frmQueue_,
                            RecorderSyncQueue *syncQueue_,
                            FFRecorder *recorder_)
{
    vFilter = vFilter_;
    vEncoder = vEncoder_;
    muxer = muxer_;
    frmQueue = frmQueue_;
    syncQueue = syncQueue_;
    recorder = recorder_;
}

void FFVEncoderThread::wakeAllThread()
{
    requestAbort();
}

void FFVEncoderThread::requestDrainStop()
{
    qDebug() << "[FFVEncoderThread] requestDrainStop";
    if (vEncoder) {
        vEncoder->requestDrainStop();
    }
}

void FFVEncoderThread::requestAbort()
{
    qDebug() << "[FFVEncoderThread] requestAbort enter";
    m_stop.store(true, std::memory_order_release);
    if (frmQueue) {
        qDebug() << "[FFVEncoderThread] close frame queue for abort";
        frmQueue->close(MediaQueueCloseReason::RequestedStop);
    }
    if (vEncoder) {
        qDebug() << "[FFVEncoderThread] abort encoder";
        vEncoder->requestAbort();
    }
    qDebug() << "[FFVEncoderThread] requestAbort leave";
}

void FFVEncoderThread::close()
{
    if (vEncoder)
        vEncoder->close();
    streamIndex = -1;
}

void FFVEncoderThread::onPauseChanged(bool pausedFlag, int64_t ts_us)
{
    std::lock_guard<std::mutex> lk(pause_mutex);
    if (pausedFlag) {
        paused.store(true, std::memory_order_release);
    } else {
        first_after_resume.store(true, std::memory_order_release);
        paused.store(false, std::memory_order_release);
    }
}

void FFVEncoderThread::run()
{
    while (!m_stop) {
        if (!frmQueue)
            break;

        auto received = frmQueue->receive();
        if (received.result == MediaQueueResult::Eof) {
            if (vEncoder && streamIndex >= 0) {
                qDebug() << "[FFVEncoderThread] frame queue finished, draining encoder";
                const int flushed = vEncoder->finish(streamIndex);
                if (flushed < 0) failEncoding(flushed);
            } else if (vEncoder) {
                qWarning() << "[FFVEncoderThread] frame queue finished without an initialized encoder";
                vEncoder->requestAbort();
            }
            break;
        }
        if (received.result != MediaQueueResult::Ok || received.item == nullptr)
            break;
        AVFrame *frame = received.item;
        if (AVFrameTraits::isNull(frame)) {
            AVFrameTraits::release(frame);
            break;
        }

        if (streamIndex == -1) {
            if (!vFilter || !vEncoder || !muxer) {
                AVFrameTraits::release(frame);
                break;
            }
            if (!initEncoder(frame)) {
                AVFrameTraits::release(frame);
                if (vEncoder) {
                    vEncoder->requestAbort();
                }
                break;
            }
        }

        if (paused.load(std::memory_order_acquire)) {
            AVFrameTraits::release(frame);
            continue;
        }

        if (first_after_resume.exchange(false, std::memory_order_acquire))
            frame->pict_type = AV_PICTURE_TYPE_I;

        int64_t vpts = 0;
        if (syncQueue) {
            const auto decision = syncQueue->reserveVideoFrame();
            if (decision.drop) {
                AVFrameTraits::release(frame);
                continue;
            }
            vpts = decision.pts;
        }

        // GPU ddagrab sources submit their yuv420p preview from the split graph. The encoder
        // receives the original D3D11 frame (and may download it internally), so submitting
        // it here would be rejected by PreviewBridge and could duplicate CPU previews.
        if (frame->format != AV_PIX_FMT_D3D11) {
            sendPreviewData(frame);
        }
        const int encoded = vEncoder->encode(frame, streamIndex, vpts, timeBase);
        if (encoded < 0) {
            AVFrameTraits::release(frame);
            failEncoding(encoded);
            break;
        }
        AVFrameTraits::release(frame);
    }
}

void FFVEncoderThread::sendPreviewData(AVFrame *frame)
{
    PreviewBridge::instance().submitFrame(frame);
}

bool FFVEncoderThread::initEncoder(AVFrame *frame)
{
    frameRate = vFilter->getFrameRate();
    const bool initialized = vEncoder->initVideo(frame, frameRate);
    const VideoEncoderSelection selection = vEncoder->videoEncoderSelection();
    publishEncoderSelection(selection);

    AVCodecContext *codecContext = vEncoder->getCodecCtx();
    if (!initialized || !selection.success || !codecContext) {
        qWarning().noquote() << QStringLiteral("[FFVEncoderThread] encoder initialization failed detail=%1")
                                         .arg(selection.detail);
        return false;
    }

    timeBase = codecContext->time_base;
    muxer->addStream(codecContext);
    streamIndex = muxer->getVStreamIndex();
    if (streamIndex < 0) {
        qWarning() << "[FFVEncoderThread] muxer did not create a video stream";
        return false;
    }

    if (syncQueue) {
        syncQueue->configureVideo(frameRate, timeBase);
    }
    return true;
}

void FFVEncoderThread::publishEncoderSelection(const VideoEncoderSelection &selection)
{
    if (!recorder) {
        return;
    }

    QMetaObject::invokeMethod(recorder,
                              "setSelectedVideoEncoder",
                              Qt::QueuedConnection,
                              Q_ARG(QString, selection.codecName),
                              Q_ARG(bool, selection.isHardware),
                              Q_ARG(QString, selection.detail));
}

void FFVEncoderThread::failEncoding(int error)
{
    m_stop.store(true);
    if (frmQueue) frmQueue->close(MediaQueueCloseReason::Error, error);
    if (recorder) {
        QMetaObject::invokeMethod(recorder, "setCaptureError", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("Video encoder failed (%1).").arg(error)),
                                  Q_ARG(QString, QStringLiteral("Recording stopped; check the output file.")));
        QMetaObject::invokeMethod(recorder, "stopOnCaptureError", Qt::QueuedConnection);
    }
    // Seal output even when the codec cannot flush; the muxer can drain prior packets.
    if (vEncoder) vEncoder->finish(streamIndex);
}

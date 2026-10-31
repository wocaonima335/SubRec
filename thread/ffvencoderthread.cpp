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
    paused.store(false); first_after_resume.store(false);
    minimumCaptureUs.store(AV_NOPTS_VALUE); captureStalled.store(false);
}

void FFVEncoderThread::onPauseChanged(bool pausedFlag, int64_t ts_us)
{
    std::lock_guard<std::mutex> lk(pause_mutex);
    if (pausedFlag) {
        paused.store(true, std::memory_order_release);
    } else {
        minimumCaptureUs.store(ts_us);
        first_after_resume.store(true, std::memory_order_release);
        paused.store(false, std::memory_order_release);
    }
}

void FFVEncoderThread::run()
{
    ticks.configure(kRecordFrameRate);
    missingStamps.store(0);
    AVFrame *lastFrame = nullptr;
    struct FrameCleanup { AVFrame *&frame; ~FrameCleanup() { av_frame_free(&frame); } } cleanup{lastFrame};
    ticks.setHoldFrameCallback([&](int64_t tick, int64_t) {
        if (!lastFrame || streamIndex < 0) return false;
        const int result = vEncoder->encode(lastFrame, streamIndex, tick, timeBase);
        if (result < 0) { failEncoding(result); return false; }
        if (syncQueue) syncQueue->observeVideoTick(tick, kRecordFrameRate);
        return true;
    });
    while (!m_stop) {
        if (!frmQueue)
            break;

        auto received = frmQueue->sharedState()->tryReceive();
        if (received.result == MediaQueueResult::Again) {
            if (captureStalled.load() && !paused.load() && !first_after_resume.load() && mediaClock)
                ticks.requestHoldover(mediaClock->nowMediaUs(), 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
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

        int64_t captureUs;
        if (!frameCaptureUs(frame, &captureUs)) {
            ++missingStamps;
            qWarning() << "[VideoClock] missing_capture_stamp count=" << missingStamps.load();
            AVFrameTraits::release(frame);
            failTimestamp("Video capture timestamp missing; P1-Z capture stamping is required.");
            break;
        }
        if (paused.load(std::memory_order_acquire) ||
            (minimumCaptureUs.load() != AV_NOPTS_VALUE && captureUs < minimumCaptureUs.load())) {
            AVFrameTraits::release(frame);
            continue;
        }
        const int64_t mediaUs = mediaClock ? mediaClock->mediaUs(captureUs) : AV_NOPTS_VALUE;
        if (first_after_resume.exchange(false, std::memory_order_acquire)) {
            if (!ticks.beginSegment(mediaUs)) {
                AVFrameTraits::release(frame); failTimestamp("Invalid video resume timestamp."); break;
            }
            frame->pict_type = AV_PICTURE_TYPE_I;
        }
        const auto decision = ticks.decide(mediaUs);
        if (!decision.accepted()) {
            AVFrameTraits::release(frame);
            if (decision.kind == TickDecision::Kind::SlotCollision) continue;
            failTimestamp("Invalid video session timestamp."); break;
        }
        if (streamIndex == -1 && (!vFilter || !vEncoder || !muxer || !initEncoder(frame))) {
            AVFrameTraits::release(frame);
            failTimestamp("Video encoder initialization failed."); break;
        }
        const int64_t vpts = decision.tick;

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
        if (frame->pict_type == AV_PICTURE_TYPE_I &&
            minimumCaptureUs.load(std::memory_order_acquire) != AV_NOPTS_VALUE) {
            qInfo() << "[VEnc] resume keyframe pts=" << vpts;
            minimumCaptureUs.store(AV_NOPTS_VALUE, std::memory_order_release);
        }
        if (syncQueue) syncQueue->observeVideoTick(vpts, kRecordFrameRate);
        av_frame_free(&lastFrame);
        lastFrame = av_frame_clone(frame);
        if (!lastFrame) {
            AVFrameTraits::release(frame); failTimestamp("Could not retain the last video frame."); break;
        }
        AVFrameTraits::release(frame);
    }
    ticks.setHoldFrameCallback({});
    const auto counts = ticks.counters();
    qInfo() << "[VideoClock] summary frames=" << counts.frames << "slotCollisions=" << counts.slotCollisions
            << "laggedTicks=" << counts.laggedTicks << "holdoverTicks=" << counts.holdoverTicks
            << "missing_capture_stamps=" << missingStamps.load();
}

void FFVEncoderThread::sendPreviewData(AVFrame *frame)
{
    PreviewBridge::instance().submitFrame(frame);
}

bool FFVEncoderThread::initEncoder(AVFrame *frame)
{
    frameRate = kRecordFrameRate;
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

    if (av_cmp_q(timeBase, av_inv_q(frameRate)) != 0) {
        qWarning() << "[VideoClock] encoder time base does not match session frame rate";
        return false;
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

void FFVEncoderThread::failTimestamp(const char *reason)
{
    m_stop.store(true);
    if (frmQueue) frmQueue->close(MediaQueueCloseReason::Error, AVERROR(EINVAL));
    if (recorder) {
        QMetaObject::invokeMethod(recorder, "setCaptureError", Qt::QueuedConnection,
            Q_ARG(QString, QString::fromUtf8(reason)),
            Q_ARG(QString, QStringLiteral("Recording stopped; check capture timestamp integration.")));
        QMetaObject::invokeMethod(recorder, "stopOnCaptureError", Qt::QueuedConnection);
    }
    if (vEncoder) {
        if (streamIndex >= 0) vEncoder->finish(streamIndex);
        else vEncoder->requestAbort();
    }
}

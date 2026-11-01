#include "ffmuxerthread.h"

#include "recorder/ffrecorder.h"

#include "event/ffprocessevent.h"
#include "muxer/ffmuxer.h"
#include "queue/ffapacketqueue.h"
#include "queue/ffpacket.h"
#include "queue/ffvpacketqueue.h"
#include "queue/packet_interleaver.h"

#include <QtCore/QtGlobal>
#include <QMetaObject>
#include <QString>
#include <QDebug>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <chrono>
#include <memory>
#include <thread>

namespace {
// Audio packets held while no video has arrived yet. Beyond this the bounded
// producer queue applies backpressure; startup audio (AAC priming) is never discarded.
constexpr size_t kBootstrapAudioPacketLimit = 24;

int64_t steadyNowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

FFMuxerThread::FFMuxerThread()
{}

FFMuxerThread::~FFMuxerThread() {}

void FFMuxerThread::init(FFAPacketQueue *aPktQueue_,
                         FFVPacketQueue *vPktQueue_,
                         FFMuxer *muxer_,
                         FFAEncoder *aEncoder_,
                         FFVEncoder *vEncoder_,
                         FFRecorder *recorderCtx_)
{
    aPktQueue = aPktQueue_;
    vPktQueue = vPktQueue_;

    // Signature retained for existing callers; no borrowed encoder state.
    (void)aEncoder_;
    (void)vEncoder_;
    errorCode.store(0);
    bootstrapMaxPackets = bootstrapHoldNs = droppedPackets = interleaveStalls = 0;
    durationAdjusted = droppedBeforeKeyframe = 0;
    finalizeResult.store(FinalizeState::NotFinalized);
    diagnosticsLogged = false;
    drainDeadlineMs.store(0);
    drainExpired.store(false);

    muxer = muxer_;

    recorderCtx = recorderCtx_;
}

void FFMuxerThread::close()
{
    if (muxer)
    {
        const int ret = muxer->close();
        if (ret < 0) {
            int expected = 0;
            errorCode.compare_exchange_strong(expected, ret);
        }
    }

    if (!diagnosticsLogged) {
        const auto s = diagnostics();
        qInfo() << "[MuxThread] closed bootstrap_max_packets=" << s.bootstrapMaxPackets
                << "bootstrap_hold_ms=" << s.bootstrapHoldNs / 1000000.0
                << "enqueue_backpressure_total_ms=" << s.enqueueBlockedNs / 1000000.0
                << "dropped_packets=" << s.droppedPackets
                << "skipped_packets=" << (muxer ? muxer->skippedPackets() : 0)
                << "rejected_writes=" << (muxer ? muxer->rejectedWrites() : 0)
                << "guardDropped=" << s.guardDropped
                << "guardDropped_audio=" << (muxer ? muxer->guardDropped(AVMEDIA_TYPE_AUDIO) : 0)
                << "guardDropped_video=" << (muxer ? muxer->guardDropped(AVMEDIA_TYPE_VIDEO) : 0)
                << "interleaveStalls=" << s.interleaveStalls
                << "duration_adjusted=" << s.durationAdjusted
                << "dropped_before_keyframe=" << s.droppedBeforeKeyframe
                << "finalize_state=" << finalizeStateName(finalizeResult.load());
        diagnosticsLogged = true;
    }
    lastProcessTime = 0;
}

FFMuxerThread::Diagnostics FFMuxerThread::diagnostics() const
{
    Diagnostics result{bootstrapMaxPackets.load(), bootstrapHoldNs.load(), 0, droppedPackets.load()};
    // Sum producer CV waits on both encoded-packet queues. This is not time
    // waiting for an empty input, and can exceed wall time if producers overlap.
    auto add = [&](auto *queue) {
        if (!queue) return;
        const auto s = queue->diagnostics();
        result.enqueueBlockedNs += s.blockedNs;
        result.droppedPackets += s.rejected + s.discarded;
    };
    add(aPktQueue); add(vPktQueue);
    result.guardDropped = muxer ? muxer->guardDropped() : 0;
    result.interleaveStalls = interleaveStalls.load();
    result.durationAdjusted = durationAdjusted.load();
    result.droppedBeforeKeyframe = droppedBeforeKeyframe.load();
    return result;
}

void FFMuxerThread::wakeAllThread()
{
    requestAbort();
}

void FFMuxerThread::requestDrainStop(std::chrono::milliseconds timeout)
{
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t unset = 0;
    drainDeadlineMs.compare_exchange_strong(unset, now + std::max<int64_t>(1, timeout.count()));
}

void FFMuxerThread::finishQueuedPackets()
{
    // sendFinish seals the queue and wakes blocked producers without discarding
    // pending packets. Unlike requestAbort, it does not cancel FFmpeg trailer I/O.
    if (aPktQueue) aPktQueue->sendFinish();
    if (vPktQueue) vPktQueue->sendFinish();
}

void FFMuxerThread::checkDrainDeadline()
{
    const auto deadline = drainDeadlineMs.load();
    if (!deadline || drainExpired.load()) return;
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now < deadline) return;
    if (!drainExpired.exchange(true)) {
        qWarning() << "[MuxThread] drain timeout; sealing packet queues and finalizing existing data";
        finishQueuedPackets();
    }
}

void FFMuxerThread::requestAbort()
{
    m_stop.store(true);
    // Before any packet was written this cancels FFmpeg I/O; afterwards it only
    // stops accepting packets and bounds the finalize, so the trailer is kept.
    if (muxer) muxer->requestAbort();
    if (vPktQueue)
    {
        vPktQueue->close(MediaQueueCloseReason::RequestedStop);
    }
    if (aPktQueue)
    {
        aPktQueue->close(MediaQueueCloseReason::RequestedStop);
    }
}

void FFMuxerThread::fail(int error)
{
    if (error == AVERROR_EXIT && m_stop.load()) {
        return;
    }
    int expected = 0;
    errorCode.compare_exchange_strong(expected, error);
    m_stop.store(true);
    if (recorderCtx) {
        QMetaObject::invokeMethod(recorderCtx, "setCaptureError", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("Muxer write failed; recording stopped.")),
                                  Q_ARG(QString, QStringLiteral("Recording stopped; attempting to finalize the file.")));
        QMetaObject::invokeMethod(recorderCtx, "stopOnCaptureError", Qt::QueuedConnection);
    }
    // close() wakes both blocked producers and consumers, unlike a plain notify.
    auto closeQueue = [error](auto *queue) {
        if (queue) queue->close(MediaQueueCloseReason::Error, error);
    };
    closeQueue(aPktQueue);
    closeQueue(vPktQueue);
    if (recorderCtx) {
        closeQueue(recorderCtx->getAFilterEncoderFrmQueue());
        closeQueue(recorderCtx->getVFilterEncoderFrmQueue());
        for (int i = 0; i < 2; ++i) {
            closeQueue(recorderCtx->getADecoderFrmQueue(i));
            closeQueue(recorderCtx->getADecoderPktQueue(i));
        }
        for (int i = 0; i < 3; ++i) {
            closeQueue(recorderCtx->getVDecoderFrmQueue(i));
            closeQueue(recorderCtx->getVDecoderPktQueue(i));
        }
    }
}

void FFMuxerThread::finalizeOutput()
{
    if (!muxer || finalizeResult.load() != FinalizeState::NotFinalized) return;
    // Writing the trailer is bounded by the stop timeout; I/O is interrupted only
    // after it expires. Recoverable means the fragments on disk remain playable.
    FinalizeState state = muxer->finalize(capture_stop::timeout());
    if (state == FinalizeState::NotFinalized) state = FinalizeState::FailedNoData;
    finalizeResult.store(state);
    qInfo() << "[MuxThread] finalize_state=" << finalizeStateName(state);
    if (state == FinalizeState::Recoverable) {
        const int ret = muxer->lastCloseResult();
        fail(ret < 0 ? ret : AVERROR(EIO));
    }
}

void FFMuxerThread::run()
{
    qDebug() << "[MuxThread] run() start. Waiting header write...";
    struct FinalizeOnExit {
        FFMuxerThread *self;
        ~FinalizeOnExit() { self->finalizeOutput(); }
    } finalizeOnExit{this};
    if (!muxer || !vPktQueue) { fail(AVERROR(EINVAL)); return; }
    if (m_stop.load()) { requestAbort(); return; }
    const int headerResult = muxer->writeHeader();
    if (headerResult < 0) { fail(headerResult); return; }
    AVRational aTB{}, vTB{};
    int timingResult = aPktQueue ? muxer->getStreamTiming(AVMEDIA_TYPE_AUDIO, &aTB) : 0;
    if (timingResult >= 0)
        timingResult = muxer->getStreamTiming(AVMEDIA_TYPE_VIDEO, &vTB);
    if (timingResult < 0) { fail(timingResult); return; }
    qDebug() << "[MuxThread] time_base: audio=" << aTB.num << "/" << aTB.den
             << ", video=" << vTB.num << "/" << vTB.den;
    // RecorderSyncQueue/MediaClock already establish the session origin. Negative
    // AAC DTS is encoder priming; positive video starts are expressed by edit lists.

    InterleaverConfig config;
    config.audioEnabled = aPktQueue != nullptr;
    config.audioTimeBase = aTB;
    config.videoTimeBase = vTB;
    config.maxPendingWhileOpposingAbsent = kBootstrapAudioPacketLimit;
    PacketInterleaver interleaver(config);
    if (!aPktQueue) interleaver.finish(StreamKind::Audio);

    auto queueFailure = [this](const MediaQueueReadResult<FFPacket> &result) {
        if (result.result == MediaQueueResult::Error)
            fail(result.errorCode < 0 ? result.errorCode : AVERROR(EIO));
        else m_stop.store(true);
    };
    // Non-blocking pull of one packet; false on a queue failure.
    auto pull = [&](auto *queue, StreamKind kind, bool &progressed) {
        if (!queue || interleaver.finished(kind) || !interleaver.wants(kind)) return true;
        auto received = queue->tryReceive();
        if (received.ok()) {
            interleaver.push(kind, received.item);
            progressed = true;
        } else if (received.result == MediaQueueResult::Eof) {
            interleaver.finish(kind);
            progressed = true;
        } else if (received.result != MediaQueueResult::Again) {
            queueFailure(received);
            return false;
        }
        return true;
    };

    uint64_t audioPacketsProcessed = 0, videoPacketsProcessed = 0;
    auto nextDiagnostic = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    int64_t holdStartUs = -1;
    bool bootstrapLogged = false;
    while (!m_stop.load()) {
        checkDrainDeadline();
        bool progressed = false;
        if (!pull(aPktQueue, StreamKind::Audio, progressed)) break;
        if (!pull(vPktQueue, StreamKind::Video, progressed)) break;

        // Startup hold: audio staged at its limit while video has not delivered yet.
        const int64_t nowUs = steadyNowUs();
        const bool holding = aPktQueue && !interleaver.everPushed(StreamKind::Video) &&
                             !interleaver.finished(StreamKind::Video) &&
                             interleaver.pending(StreamKind::Audio) >= kBootstrapAudioPacketLimit;
        if (holding && holdStartUs < 0) holdStartUs = nowUs;
        if (!holding && holdStartUs >= 0) {
            bootstrapHoldNs += static_cast<uint64_t>(nowUs - holdStartUs) * 1000;
            holdStartUs = -1;
        }
        if (!bootstrapLogged && interleaver.everPushed(StreamKind::Video)) {
            bootstrapLogged = true;
            qDebug() << "[MuxThread] first video packet; staged audio packets="
                     << interleaver.stats().maxStartupPending[0];
        }

        bool writeFailed = false;
        StreamKind kind;
        while (FFPacket *packet = interleaver.pop(nowUs, &kind)) {
            progressed = true;
            const bool audio = kind == StreamKind::Audio;
            const AVRational tb = audio ? aTB : vTB;
            const double seconds = packet->packet.pts == AV_NOPTS_VALUE ? NAN : packet->packet.pts * av_q2d(tb);
            const int ret = muxer->mux(&packet->packet);
            FFPacketTraits::release(packet);
            if (ret < 0) { ++droppedPackets; fail(ret); writeFailed = true; break; }
            ++(audio ? audioPacketsProcessed : videoPacketsProcessed);
            if (seconds - lastProcessTime >= 0.01) {
                sendCaptureProcessEvent(seconds);
                lastProcessTime = seconds;
            }
        }
        const auto &stats = interleaver.stats();
        bootstrapMaxPackets.store(stats.maxStartupPending[0]);
        interleaveStalls.store(stats.stallReleases);
        durationAdjusted.store(stats.durationAdjusted[0] + stats.durationAdjusted[1]);
        droppedBeforeKeyframe.store(stats.droppedBeforeKeyframe);
        if (writeFailed) break;
        if (interleaver.finished(StreamKind::Audio) && interleaver.finished(StreamKind::Video)) {
            // Both streams ended: write the remaining lookahead packets in order.
            while (FFPacket *packet = interleaver.popForFlush(&kind)) {
                const int ret = muxer->mux(&packet->packet);
                FFPacketTraits::release(packet);
                if (ret < 0) { ++droppedPackets; fail(ret); break; }
                ++(kind == StreamKind::Audio ? audioPacketsProcessed : videoPacketsProcessed);
            }
            break;
        }

        const auto diagnosticNow = std::chrono::steady_clock::now();
        if (diagnosticNow >= nextDiagnostic) {
            nextDiagnostic = diagnosticNow + std::chrono::minutes(1);
            qInfo() << "[RecordingBuffers] audio_packets=" << audioPacketsProcessed
                    << "video_packets=" << videoPacketsProcessed
                    << "audio_packet_queue=" << (aPktQueue ? aPktQueue->length() : 0)
                    << "video_packet_queue=" << (vPktQueue ? vPktQueue->length() : 0)
                    << "skipped_packets=" << muxer->skippedPackets()
                    << "stall_releases=" << stats.stallReleases;
        }
        if (!progressed) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (holdStartUs >= 0) bootstrapHoldNs += static_cast<uint64_t>(steadyNowUs() - holdStartUs) * 1000;

    // Stop/abort/failure: packets still staged here were never written.
    StreamKind kind;
    while (FFPacket *packet = interleaver.popForFlush(&kind)) {
        ++droppedPackets;
        FFPacketTraits::release(packet);
    }
    qDebug() << "[MuxThread] loop end; audio=" << audioPacketsProcessed
             << "video=" << videoPacketsProcessed << "; finalizing.";
}

void FFMuxerThread::sendCaptureProcessEvent(double seconds)
{
    if (!recorderCtx) {
        return;
    }

    FFCaptureProcessEvent captureProcessEvent(recorderCtx, seconds);
    captureProcessEvent.work();
}

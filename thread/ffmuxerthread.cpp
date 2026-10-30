#include "ffmuxerthread.h"

#include "recorder/ffrecorder.h"

#include "event/ffprocessevent.h"
#include "muxer/ffmuxer.h"
#include "queue/ffapacketqueue.h"
#include "queue/ffpacket.h"
#include "queue/ffvpacketqueue.h"

#include <QtCore/QtGlobal>
#include <QDebug>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <chrono>
#include <deque>
#include <thread>

namespace {
constexpr AVRational kUsecTimeBase{1, AV_TIME_BASE};
constexpr size_t kBootstrapAudioPacketLimit = 24;

int64_t packetAnchorTs(const AVPacket *packet)
{
    if (!packet) {
        return AV_NOPTS_VALUE;
    }

    if (packet->dts != AV_NOPTS_VALUE) {
        return packet->dts;
    }
    return packet->pts;
}

int64_t packetAnchorUs(const AVPacket *packet, AVRational timeBase)
{
    const int64_t ts = packetAnchorTs(packet);
    if (ts == AV_NOPTS_VALUE) {
        return AV_NOPTS_VALUE;
    }
    return av_rescale_q(ts, timeBase, kUsecTimeBase);
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
    bootstrapMaxPackets = bootstrapHoldNs = droppedPackets = 0;
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
                << "dropped_packets=" << s.droppedPackets;
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

template<class Queue> auto FFMuxerThread::receivePacket(Queue *queue)
{
    // Polling permits a stop deadline to be armed while already waiting. Idle
    // recording and pause have no deadline. DTS merge order remains unchanged.
    for (;;) {
        checkDrainDeadline();
        auto result = queue->tryReceive();
        if (result.result != MediaQueueResult::Again) return result;
        if (m_stop.load()) {
            result.result = MediaQueueResult::Closed;
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void FFMuxerThread::requestAbort()
{
    m_stop.store(true);
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
        requestAbort();
        return;
    }
    int expected = 0;
    errorCode.compare_exchange_strong(expected, error);
    m_stop.store(true);
    if (muxer) muxer->requestAbort();
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

void FFMuxerThread::run()
{
    qDebug() << "[MuxThread] run() start. Waiting header write...";
    if (!muxer || !vPktQueue) { fail(AVERROR(EINVAL)); return; }
    if (m_stop.load()) { requestAbort(); return; }
    const int headerResult = muxer->writeHeader();
    if (headerResult < 0) { fail(headerResult); return; }
    AVRational aTB{}, vTB{};
    int timingResult = aPktQueue ? muxer->getStreamTiming(AVMEDIA_TYPE_AUDIO, &aTB) : 0;
    if (timingResult >= 0)
        timingResult = muxer->getStreamTiming(AVMEDIA_TYPE_VIDEO, &vTB);
    if (timingResult < 0) { fail(timingResult); return; }
    auto queueFailure = [this](const MediaQueueReadResult<FFPacket> &result) {
        if (result.result == MediaQueueResult::Error)
            fail(result.errorCode < 0 ? result.errorCode : AVERROR(EIO));
        else requestAbort();
    };
    auto finish = [this]() {
        const int ret = muxer->writeTrailer();
        if (ret < 0) fail(ret);
    };

    std::deque<FFPacket *> bootstrapAudioPackets;
    auto clearBootstrapAudio = [this, &bootstrapAudioPackets]() {
        while (!bootstrapAudioPackets.empty()) {
            FFPacket *pkt = bootstrapAudioPackets.front();
            bootstrapAudioPackets.pop_front();
            ++droppedPackets;
            FFPacketTraits::release(pkt);
        }
    };
    auto pushBootstrapAudio = [this, &bootstrapAudioPackets](FFPacket *pkt) {
        if (!pkt) {
            return;
        }
        bootstrapAudioPackets.push_back(pkt);
        bootstrapMaxPackets.store(std::max<uint64_t>(bootstrapMaxPackets.load(), bootstrapAudioPackets.size()));
    };
    auto nextAudioPacket = [this, &bootstrapAudioPackets]() -> MediaQueueReadResult<FFPacket> {
        if (!bootstrapAudioPackets.empty()) {
            FFPacket *pkt = bootstrapAudioPackets.front();
            bootstrapAudioPackets.pop_front();
            return MediaQueueReadResult<FFPacket>{
                MediaQueueResult::Ok, pkt, MediaQueueCloseReason::None, 0};
        }
        return receivePacket(aPktQueue);
    };

    bool audioFinished = !aPktQueue;
    bool videoFinished = false;
    bool audioBootstrapFailed = false;
    bool videoBootstrapFailed = false;
    FFPacket *audioPkt = nullptr;
    FFPacket *videoPkt = nullptr;

    auto holdStart = std::chrono::steady_clock::time_point{};
    while (!m_stop && !videoPkt && !videoFinished) {
        checkDrainDeadline();
        // Preserve the first AAC packet (including priming). Once this bounded
        // staging area fills, leave packets in the bounded producer queue and
        // apply backpressure until video arrives; never discard startup audio.
        if (!audioFinished && bootstrapAudioPackets.size() < kBootstrapAudioPacketLimit) {
            const auto audioBootstrap = aPktQueue->tryReceive();
            if (audioBootstrap.ok()) {
                pushBootstrapAudio(audioBootstrap.item);
            } else if (audioBootstrap.result == MediaQueueResult::Eof) {
                audioFinished = true;
            } else if (audioBootstrap.result != MediaQueueResult::Again) {
                audioBootstrapFailed = true;
                queueFailure(audioBootstrap);
                break;
            }
        }

        if (bootstrapAudioPackets.size() == kBootstrapAudioPacketLimit &&
            holdStart == std::chrono::steady_clock::time_point{})
            holdStart = std::chrono::steady_clock::now();
        const auto videoBootstrap = vPktQueue->tryReceive();
        if (videoBootstrap.ok()) {
            videoPkt = videoBootstrap.item;
            break;
        }
        if (videoBootstrap.result == MediaQueueResult::Eof) {
            videoFinished = true;
            break;
        }
        if (videoBootstrap.result != MediaQueueResult::Again) {
            videoBootstrapFailed = true;
            queueFailure(videoBootstrap);
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (holdStart != std::chrono::steady_clock::time_point{})
        bootstrapHoldNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-holdStart).count();
    if (m_stop.load() || audioBootstrapFailed || videoBootstrapFailed) {
        if (videoPkt) ++droppedPackets;
        FFPacketTraits::release(videoPkt);
        clearBootstrapAudio();
        return;
    }
    if (!bootstrapAudioPackets.empty()) {
        audioPkt = bootstrapAudioPackets.front();
        bootstrapAudioPackets.pop_front();
    } else if (!audioFinished && !audioBootstrapFailed) {
        const auto audioFirst = receivePacket(aPktQueue);
        audioFinished = audioFirst.result == MediaQueueResult::Eof;
        audioPkt = audioFirst.ok() ? audioFirst.item : nullptr;
        if (!audioPkt && !audioFinished && audioFirst.result != MediaQueueResult::Again) {
            audioBootstrapFailed = true;
            queueFailure(audioFirst);
        }
    }

    if (!m_stop.load() && !videoPkt && !videoFinished && !videoBootstrapFailed) {
        const auto videoFirst = receivePacket(vPktQueue);
        videoFinished = videoFirst.result == MediaQueueResult::Eof;
        videoPkt = videoFirst.ok() ? videoFirst.item : nullptr;
        if (!videoPkt && !videoFinished && videoFirst.result != MediaQueueResult::Again) {
            videoBootstrapFailed = true;
            queueFailure(videoFirst);
        }
    }

    if (audioBootstrapFailed || videoBootstrapFailed
        || ((!audioPkt && !audioFinished) || (!videoPkt && !videoFinished))) {
        if (audioPkt) ++droppedPackets;
        if (videoPkt) ++droppedPackets;
        FFPacketTraits::release(audioPkt);
        FFPacketTraits::release(videoPkt);
        clearBootstrapAudio();
        return;
    }
    if (!audioPkt && !videoPkt) {
        clearBootstrapAudio();
        finish();
        return;
    }

    auto ptsToSec = [](int64_t pts, AVRational tb) -> double
    {
        if (pts == AV_NOPTS_VALUE)
            return NAN;
        return pts * av_q2d(tb);
    };

    qDebug() << "[MuxThread] time_base: audio=" << aTB.num << "/" << aTB.den
             << ", video=" << vTB.num << "/" << vTB.den;

    const int64_t firstAudioUs = audioPkt ? packetAnchorUs(&audioPkt->packet, aTB) : AV_NOPTS_VALUE;
    const int64_t firstVideoUs = videoPkt ? packetAnchorUs(&videoPkt->packet, vTB) : AV_NOPTS_VALUE;
    // RecorderSyncQueue (or the encoder-thread fallback) already establishes
    // the session origin. Negative AAC DTS/PTS is encoder priming, and negative
    // video DTS can be reorder delay, not a new origin. Preserve both, including
    // positive video start offsets; movenc expresses them using edit lists.
    qDebug() << "[MuxThread] preserve session timeline: firstAudio(us)=" << firstAudioUs
             << " firstVideo(us)=" << firstVideoUs;

    while (!m_stop)
    {
        checkDrainDeadline();
        AVPacket *aPacket = audioPkt ? &audioPkt->packet : nullptr;
        AVPacket *vPacket = videoPkt ? &videoPkt->packet : nullptr;

        if (!audioPkt && !videoPkt) {
            break;
        }

        const double aSec = aPacket ? ptsToSec(aPacket->pts, aTB) : NAN;
        const double vSec = vPacket ? ptsToSec(vPacket->pts, vTB) : NAN;

        const auto aOrder = packetAnchorTs(aPacket);
        const auto vOrder = packetAnchorTs(vPacket);
        bool chooseAudio = false;
        if (!videoPkt)
        {
            chooseAudio = true;
        }
        else if (!audioPkt)
        {
            chooseAudio = false;
        }
        else if (aOrder == AV_NOPTS_VALUE && vOrder == AV_NOPTS_VALUE)
        {
            chooseAudio = true;
        }
        else if (aOrder == AV_NOPTS_VALUE)
        {
            chooseAudio = false;
        }
        else if (vOrder == AV_NOPTS_VALUE)
        {
            chooseAudio = true;
        }
        else
        {
            chooseAudio = av_compare_ts(aOrder, aTB, vOrder, vTB) <= 0;
        }

        int ret;
        double processedSec;
        if (chooseAudio)
        {
            ret = muxer->mux(aPacket);
            processedSec = aSec;
            FFPacketTraits::release(audioPkt);
            if (ret < 0) { ++droppedPackets; fail(ret); break; }
            auto nextAudio = nextAudioPacket();
            if (nextAudio.result == MediaQueueResult::Eof) {
                audioFinished = true;
                audioPkt = nullptr;
            } else if (!nextAudio.ok()) {
                audioPkt = nullptr;
                queueFailure(nextAudio);
                break;
            } else {
                audioPkt = nextAudio.item;
            }
        }
        else
        {
            ret = muxer->mux(vPacket);
            processedSec = vSec;
            FFPacketTraits::release(videoPkt);
            if (ret < 0) { ++droppedPackets; fail(ret); break; }
            auto nextVideo = receivePacket(vPktQueue);
            if (nextVideo.result == MediaQueueResult::Eof) {
                videoFinished = true;
                videoPkt = nullptr;
            } else if (!nextVideo.ok()) {
                videoPkt = nullptr;
                queueFailure(nextVideo);
                break;
            } else {
                videoPkt = nextVideo.item;
            }
        }

        if (processedSec - lastProcessTime >= 0.01)
        {
            sendCaptureProcessEvent(processedSec);
            lastProcessTime = processedSec;
        }
    }

    if (audioPkt) { ++droppedPackets; FFPacketTraits::release(audioPkt); }
    if (videoPkt) { ++droppedPackets; FFPacketTraits::release(videoPkt); }
    clearBootstrapAudio();
    qDebug() << "[MuxThread] loop end; trailer only on normal drain.";
    if (!m_stop.load()) finish();
}

void FFMuxerThread::sendCaptureProcessEvent(double seconds)
{
    if (!recorderCtx) {
        return;
    }

    FFCaptureProcessEvent captureProcessEvent(recorderCtx, seconds);
    captureProcessEvent.work();
}

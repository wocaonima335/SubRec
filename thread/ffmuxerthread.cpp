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

#define CAPTURE_TIME 60

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

int64_t rebaseTimestamp(int64_t ts, AVRational timeBase, int64_t baseUs)
{
    if (ts == AV_NOPTS_VALUE || baseUs == AV_NOPTS_VALUE) {
        return ts;
    }

    int64_t tsUs = av_rescale_q(ts, timeBase, kUsecTimeBase) - baseUs;
    if (tsUs < 0) {
        tsUs = 0;
    }

    return av_rescale_q_rnd(
        tsUs, kUsecTimeBase, timeBase,
        static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
}

void normalizePacketTimeline(AVPacket *packet, AVRational timeBase, int64_t baseUs)
{
    if (!packet || baseUs == AV_NOPTS_VALUE) {
        return;
    }

    packet->pts = rebaseTimestamp(packet->pts, timeBase, baseUs);
    packet->dts = rebaseTimestamp(packet->dts, timeBase, baseUs);

    if (packet->pts == AV_NOPTS_VALUE && packet->dts != AV_NOPTS_VALUE) {
        packet->pts = packet->dts;
    } else if (packet->dts == AV_NOPTS_VALUE && packet->pts != AV_NOPTS_VALUE) {
        packet->dts = packet->pts;
    }

    if (packet->pts != AV_NOPTS_VALUE && packet->dts != AV_NOPTS_VALUE && packet->dts > packet->pts) {
        packet->dts = packet->pts;
    }
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

    lastProcessTime = 0;
}

void FFMuxerThread::wakeAllThread()
{
    requestAbort();
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
    if (!muxer || !aPktQueue || !vPktQueue) { fail(AVERROR(EINVAL)); return; }
    if (m_stop.load()) { requestAbort(); return; }
    const int headerResult = muxer->writeHeader();
    if (headerResult < 0) { fail(headerResult); return; }
    AVRational aTB{}, vTB{}, vFPS{};
    int timingResult = muxer->getStreamTiming(AVMEDIA_TYPE_AUDIO, &aTB);
    if (timingResult >= 0)
        timingResult = muxer->getStreamTiming(AVMEDIA_TYPE_VIDEO, &vTB, &vFPS);
    if (timingResult < 0) { fail(timingResult); return; }
    if (vFPS.num <= 0 || vFPS.den <= 0) vFPS = AVRational{30, 1};
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
    auto clearBootstrapAudio = [&bootstrapAudioPackets]() {
        while (!bootstrapAudioPackets.empty()) {
            FFPacket *pkt = bootstrapAudioPackets.front();
            bootstrapAudioPackets.pop_front();
            FFPacketTraits::release(pkt);
        }
    };
    auto pushBootstrapAudio = [&bootstrapAudioPackets](FFPacket *pkt) {
        if (!pkt) {
            return;
        }
        bootstrapAudioPackets.push_back(pkt);
        while (bootstrapAudioPackets.size() > kBootstrapAudioPacketLimit) {
            FFPacket *dropPkt = bootstrapAudioPackets.front();
            bootstrapAudioPackets.pop_front();
            FFPacketTraits::release(dropPkt);
        }
    };
    auto nextAudioPacket = [this, &bootstrapAudioPackets]() -> MediaQueueReadResult<FFPacket> {
        if (!bootstrapAudioPackets.empty()) {
            FFPacket *pkt = bootstrapAudioPackets.front();
            bootstrapAudioPackets.pop_front();
            return MediaQueueReadResult<FFPacket>{
                MediaQueueResult::Ok, pkt, MediaQueueCloseReason::None, 0};
        }
        return aPktQueue->receive();
    };

    bool audioFinished = false;
    bool videoFinished = false;
    bool audioBootstrapFailed = false;
    bool videoBootstrapFailed = false;
    FFPacket *audioPkt = nullptr;
    FFPacket *videoPkt = nullptr;

    while (!m_stop && !videoPkt && !videoFinished) {
        if (!audioFinished) {
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

    if (m_stop.load() || audioBootstrapFailed || videoBootstrapFailed) {
        FFPacketTraits::release(videoPkt);
        clearBootstrapAudio();
        return;
    }
    if (!bootstrapAudioPackets.empty()) {
        audioPkt = bootstrapAudioPackets.front();
        bootstrapAudioPackets.pop_front();
    } else if (!audioFinished && !audioBootstrapFailed) {
        const auto audioFirst = aPktQueue->receive();
        audioFinished = audioFirst.result == MediaQueueResult::Eof;
        audioPkt = audioFirst.ok() ? audioFirst.item : nullptr;
        if (!audioPkt && !audioFinished && audioFirst.result != MediaQueueResult::Again) {
            audioBootstrapFailed = true;
            queueFailure(audioFirst);
        }
    }

    if (!m_stop.load() && !videoPkt && !videoFinished && !videoBootstrapFailed) {
        const auto videoFirst = vPktQueue->receive();
        videoFinished = videoFirst.result == MediaQueueResult::Eof;
        videoPkt = videoFirst.ok() ? videoFirst.item : nullptr;
        if (!videoPkt && !videoFinished && videoFirst.result != MediaQueueResult::Again) {
            videoBootstrapFailed = true;
            queueFailure(videoFirst);
        }
    }

    if (audioBootstrapFailed || videoBootstrapFailed
        || ((!audioPkt && !audioFinished) || (!videoPkt && !videoFinished))) {
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

    bool audioNormalized = false;
    bool videoNormalized = false;

    int64_t vFrameDur = av_rescale_q(1, av_inv_q(vFPS), vTB);

    if (audioPkt && videoPkt) {
        AVPacket *aPacket = &audioPkt->packet;
        AVPacket *vPacket = &videoPkt->packet;
        int64_t vPts0 = vPacket->pts;
        while (!m_stop && aPacket &&
               av_compare_ts(aPacket->pts, aTB, vPts0 - vFrameDur, vTB) < 0)
        {
            FFPacketTraits::release(audioPkt);
            auto nextAudio = nextAudioPacket();
            if (nextAudio.result == MediaQueueResult::Eof) {
                audioFinished = true;
                audioPkt = nullptr;
                break;
            }
            if (!nextAudio.ok()) {
                queueFailure(nextAudio);
                audioPkt = nullptr;
                break;
            }
            audioPkt = nextAudio.item;
            aPacket = &audioPkt->packet;
        }
    }

    if (!audioPkt && !videoPkt)
    {
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
    int64_t baseUs = AV_NOPTS_VALUE;
    if (firstAudioUs != AV_NOPTS_VALUE && firstVideoUs != AV_NOPTS_VALUE) {
        baseUs = std::min(firstAudioUs, firstVideoUs);
    } else if (firstAudioUs != AV_NOPTS_VALUE) {
        baseUs = firstAudioUs;
    } else if (firstVideoUs != AV_NOPTS_VALUE) {
        baseUs = firstVideoUs;
    }

    qDebug() << "[MuxThread] normalize base(us)=" << baseUs
             << " firstAudio(us)=" << firstAudioUs
             << " firstVideo(us)=" << firstVideoUs;

    while (!m_stop)
    {
        AVPacket *aPacket = audioPkt ? &audioPkt->packet : nullptr;
        AVPacket *vPacket = videoPkt ? &videoPkt->packet : nullptr;

        if (!audioPkt && !videoPkt) {
            break;
        }

        if (!audioNormalized) {
            normalizePacketTimeline(aPacket, aTB, baseUs);
            audioNormalized = true;
        }
        if (!videoNormalized) {
            normalizePacketTimeline(vPacket, vTB, baseUs);
            videoNormalized = true;
        }

        const double aSec = aPacket ? ptsToSec(aPacket->pts, aTB) : NAN;
        const double vSec = vPacket ? ptsToSec(vPacket->pts, vTB) : NAN;

        bool chooseAudio = false;
        if (!videoPkt)
        {
            chooseAudio = true;
        }
        else if (!audioPkt)
        {
            chooseAudio = false;
        }
        else if (aPacket->pts == AV_NOPTS_VALUE && vPacket->pts == AV_NOPTS_VALUE)
        {
            chooseAudio = true;
        }
        else if (aPacket->pts == AV_NOPTS_VALUE)
        {
            chooseAudio = false;
        }
        else if (vPacket->pts == AV_NOPTS_VALUE)
        {
            chooseAudio = true;
        }
        else
        {
            chooseAudio = av_compare_ts(aPacket->pts, aTB, vPacket->pts, vTB) <= 0;
        }

        int ret;
        double processedSec;
        if (chooseAudio)
        {
            ret = muxer->mux(aPacket);
            processedSec = aSec;
            FFPacketTraits::release(audioPkt);
            if (ret < 0) { fail(ret); break; }
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
            audioNormalized = false;
        }
        else
        {
            ret = muxer->mux(vPacket);
            processedSec = vSec;
            FFPacketTraits::release(videoPkt);
            if (ret < 0) { fail(ret); break; }
            auto nextVideo = vPktQueue->receive();
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
            videoNormalized = false;
        }

        if (processedSec - lastProcessTime >= 0.01)
        {
            sendCaptureProcessEvent(processedSec);
            lastProcessTime = processedSec;
        }
    }

    if (audioPkt)
        FFPacketTraits::release(audioPkt);
    if (videoPkt)
        FFPacketTraits::release(videoPkt);
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

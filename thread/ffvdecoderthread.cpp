#include "logging/frame_log.h"
#include "ffvdecoderthread.h"

#include "decoder/ffvdecoder.h"
#include "queue/ffpacket.h"
#include "queue/ffvpacketqueue.h"

#include <QCryptographicHash>

namespace {
std::atomic<int> g_vdecPacketMd5LogCount{0};

QByteArray packetMd5(const AVPacket *packet)
{
    if (!packet || !packet->data || packet->size <= 0) {
        return {};
    }

    return QCryptographicHash::hash(
               QByteArray(reinterpret_cast<const char *>(packet->data), packet->size),
               QCryptographicHash::Md5)
        .toHex();
}
}

FFVDecoderThread::FFVDecoderThread()
{
    stopFlag = true;
}

FFVDecoderThread::~FFVDecoderThread()
{
    vPktQueue = nullptr;
    vDecoder = nullptr;
}

void FFVDecoderThread::init(FFVDecoder *vDecoder_, FFVPacketQueue *vPktQueue_)
{
    vDecoder = vDecoder_;
    vPktQueue = vPktQueue_;
    g_vdecPacketMd5LogCount.store(0, std::memory_order_release);
}

void FFVDecoderThread::wakeAllThread()
{
    requestAbort();
}

void FFVDecoderThread::requestDrainStop()
{
    qDebug() << "[FFVDecoderThread] requestDrainStop";
    if (vDecoder) {
        vDecoder->requestDrainStop();
    }
}

void FFVDecoderThread::requestAbort()
{
    if (vDecoder) {
        vDecoder->requestAbort();
    }
    m_stop.store(true, std::memory_order_release);
    if (vPktQueue) {
        qDebug() << "[FFVDecoderThread] close packet queue for abort";
        vPktQueue->close(MediaQueueCloseReason::RequestedStop);
    }
}

void FFVDecoderThread::close()
{
    if (vDecoder) {
        vDecoder->close();
    }
    stopFlag.store(true, std::memory_order_release);
}

bool FFVDecoderThread::peekStop()
{
    return stopFlag.load(std::memory_order_acquire);
}

void FFVDecoderThread::run()
{
    while (!m_stop) {
        auto received = vPktQueue->receive();
        if (received.result == MediaQueueResult::Eof) {
            qDebug() << "[FFVDecoderThread] packet queue finished";
            vDecoder->decode(nullptr);
            if (!m_stop.load(std::memory_order_acquire)) {
                vDecoder->enqueueNull();
            }
            break;
        }
        if (received.result != MediaQueueResult::Ok || received.item == nullptr) {
            qDebug() << "[FFVDecoderThread] receive exit result="
                     << mediaQueueResultName(received.result);
            break;
        }
        FFPacket *pkt = received.item;

        if (pkt->serial != vPktQueue->getSerial()) {
            vPktQueue->flushQueue();
            vDecoder->flushDecoder();
            FFPacketTraits::release(pkt);
        } else {
            const int packetLogIndex =
                g_vdecPacketMd5LogCount.fetch_add(1, std::memory_order_relaxed) + 1;
            if (packetLogIndex <= 60) {
                qCDebug(subrec::logging::frameLog).noquote()
                    << "[VDecPacketMd5]"
                    << "index=" << packetLogIndex
                    << "pts=" << pkt->packet.pts
                    << "size=" << pkt->packet.size
                    << "md5=" << packetMd5(&pkt->packet);
            }
            vDecoder->decode(&pkt->packet);
            FFPacketTraits::release(pkt);
        }
    }
    qDebug() << "[FFVDecoderThread] run exit";
}

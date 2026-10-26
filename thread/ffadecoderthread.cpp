#include "ffadecoderthread.h"

#include "decoder/ffadecoder.h"
#include "queue/ffapacketqueue.h"
#include "queue/ffpacket.h"

FFADecoderThread::FFADecoderThread()
    : stopFlag(false)
{}

FFADecoderThread::~FFADecoderThread()
{
    aPktQueue = nullptr;
    aDecoder = nullptr;
}

void FFADecoderThread::init(FFADecoder *aDecoder_, FFAPacketQueue *aPktQueue_)
{
    aDecoder = aDecoder_;
    aPktQueue = aPktQueue_;
}

void FFADecoderThread::wakeAllThread()
{
    requestAbort();
}

void FFADecoderThread::requestDrainStop()
{
    qDebug() << "[FFADecoderThread] requestDrainStop";
    if (aDecoder) {
        aDecoder->requestDrainStop();
    }
}

void FFADecoderThread::requestAbort()
{
    if (aDecoder) {
        aDecoder->requestAbort();
    }
    m_stop.store(true, std::memory_order_release);
    if (aPktQueue) {
        qDebug() << "[FFADecoderThread] close packet queue for abort";
        aPktQueue->close(MediaQueueCloseReason::RequestedStop);
    }
}

void FFADecoderThread::close()
{
    if (aDecoder) {
        aDecoder->close();
    }
    stopFlag.store(true, std::memory_order_release);
}

bool FFADecoderThread::peekStop()
{
    return stopFlag.load(std::memory_order_acquire);
}

void FFADecoderThread::run()
{
    while (!m_stop) {
        stopFlag.store(false, std::memory_order_release);
        auto received = aPktQueue->receive();
        if (received.result == MediaQueueResult::Eof) {
            qDebug() << "[FFADecoderThread] packet queue finished";
            aDecoder->decode(nullptr);
            if (!m_stop.load(std::memory_order_acquire)) {
                aDecoder->enqueueNull();
            }
            break;
        }
        if (received.result != MediaQueueResult::Ok || received.item == nullptr) {
            qDebug() << "[FFADecoderThread] receive exit result="
                     << mediaQueueResultName(received.result);
            break;
        }
        FFPacket *pkt = received.item;

        if (pkt->serial != aPktQueue->getSerial()) {
            aPktQueue->flushQueue();
            aDecoder->flushQueue();
            aDecoder->flushDecoder();
            FFPacketTraits::release(pkt);
        } else {
            aDecoder->decode(&pkt->packet);
            FFPacketTraits::release(pkt);
        }
    }
}

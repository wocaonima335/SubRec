#include "ffapacketqueue.h"
#include "ffpacket.h"

#define MAX_PACKET_SIZE 3

FFAPacketQueue::FFAPacketQueue()
    : serial(0)
    , impl(new FFBoundedQueue<FFPacket, FFPacketTraits>())
{}

FFAPacketQueue::~FFAPacketQueue()
{
    close();
    delete impl;
}

MediaQueueReadResult<FFPacket> FFAPacketQueue::receive()
{
    return impl->receive();
}

FFPacket *FFAPacketQueue::dequeue()
{
    return receive().item;
}

MediaQueueReadResult<FFPacket> FFAPacketQueue::tryReceive()
{
    return impl->tryReceive();
}

FFPacket *FFAPacketQueue::tryDequeue()
{
    return tryReceive().item;
}

FFPacket *FFAPacketQueue::peekQueue() const
{
    return impl->peekQueue();
}

FFPacket *FFAPacketQueue::peekBack() const
{
    return impl->peekBack();
}

void FFAPacketQueue::enqueue(AVPacket *pkt)
{
    FFPacket *ffpkt = static_cast<FFPacket *>(av_mallocz(sizeof(FFPacket)));
    av_packet_move_ref(&ffpkt->packet, pkt);
    ffpkt->serial = serial.load();
    ffpkt->type = NORMAL;
    impl->enqueueFromSrc(ffpkt);
}

void FFAPacketQueue::enqueueFlush()
{
    FFPacket *ffpkt = static_cast<FFPacket *>(av_mallocz(sizeof(FFPacket)));
    ffpkt->type = FLUSH;
    ffpkt->serial = serial++;
    impl->enqueueFromSrc(ffpkt);
    ;
}

MediaQueueResult FFAPacketQueue::sendFinish()
{
    qDebug() << "[FFAPacketQueue] sendFinish serial=" << serial.load();
    return impl->sendFinish();
}

void FFAPacketQueue::receiveFinish()
{
    qDebug() << "[FFAPacketQueue] receiveFinish serial=" << serial.load();
    impl->receiveFinish();
}

void FFAPacketQueue::enqueueNull()
{
    sendFinish();
}

void FFAPacketQueue::flushQueue()
{
    while (true) {
        FFPacket *pkt = impl->peekQueue();
        if (pkt == nullptr || pkt->serial == serial.load()) {
            break;
        }
        pkt = impl->dequeue();
        if (!pkt)
            break;
        FFPacketTraits::release(pkt);
    }
}

size_t FFAPacketQueue::getSerial()
{
    return serial.load();
}

void FFAPacketQueue::clearQueue()
{
    impl->clearQueue();
}

bool FFAPacketQueue::isFinished() const
{
    return impl->isFinished();
}

bool FFAPacketQueue::isClosed() const
{
    return impl->isClosed();
}

MediaQueueCloseReason FFAPacketQueue::closeReason() const
{
    return impl->closeReason();
}

int FFAPacketQueue::errorCode() const
{
    return impl->errorCode();
}

void FFAPacketQueue::wakeAllThread()
{
    close(MediaQueueCloseReason::RequestedStop);
}

void FFAPacketQueue::close(MediaQueueCloseReason reason, int errorCode)
{
    qDebug() << "[FFAPacketQueue] close reason=" << mediaQueueCloseReasonName(reason)
             << "errorCode=" << errorCode;
    impl->close(reason, errorCode);
}

void FFAPacketQueue::start()
{
    impl->start();
}

int FFAPacketQueue::length()
{
    return impl->length();
}

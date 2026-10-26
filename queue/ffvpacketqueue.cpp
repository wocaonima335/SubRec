#include "ffvpacketqueue.h"
#include "ffpacket.h"

FFVPacketQueue::FFVPacketQueue()
    : serial(0)
    , impl(new FFBoundedQueue<FFPacket, FFPacketTraits>(30))

{}

FFVPacketQueue::~FFVPacketQueue()
{
    close();
    delete impl;
}

MediaQueueReadResult<FFPacket> FFVPacketQueue::receive()
{
    return impl->receive();
}

FFPacket *FFVPacketQueue::dequeue()
{
    return receive().item;
}

FFPacket *FFVPacketQueue::peekQueue() const
{
    return impl->peekQueue();
}

FFPacket *FFVPacketQueue::peekBack() const
{
    return impl->peekBack();
}

void FFVPacketQueue::enqueue(AVPacket *pkt)
{
    FFPacket *ffpkt = static_cast<FFPacket *>(av_mallocz(sizeof(FFPacket)));
    av_packet_move_ref(&ffpkt->packet, pkt);
    ffpkt->serial = serial.load();
    ffpkt->type = NORMAL;
    impl->enqueueFromSrc(ffpkt);
}

void FFVPacketQueue::enqueueFlush()
{
    FFPacket *ffpkt = static_cast<FFPacket *>(av_mallocz(sizeof(FFPacket)));
    ffpkt->type = FLUSH;
    ffpkt->serial = serial++; // 与原实现保持一致
    impl->enqueueFromSrc(ffpkt);
}

MediaQueueResult FFVPacketQueue::sendFinish()
{
    qDebug() << "[FFVPacketQueue] sendFinish serial=" << serial.load();
    return impl->sendFinish();
}

void FFVPacketQueue::receiveFinish()
{
    qDebug() << "[FFVPacketQueue] receiveFinish serial=" << serial.load();
    impl->receiveFinish();
}

void FFVPacketQueue::enqueueNull()
{
    sendFinish();
}

void FFVPacketQueue::flushQueue()
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

bool FFVPacketQueue::tryEnqueue(AVPacket *pkt)
{
    FFPacket *ffpkt = static_cast<FFPacket *>(av_mallocz(sizeof(FFPacket)));
    av_packet_move_ref(&ffpkt->packet, pkt);
    ffpkt->serial = serial.load();
    ffpkt->type = NORMAL;
    return impl->tryEnqueueFromSrc(ffpkt);
}

MediaQueueReadResult<FFPacket> FFVPacketQueue::tryReceive()
{
    return impl->tryReceive();
}

FFPacket *FFVPacketQueue::tryDequeue()
{
    return tryReceive().item;
}

size_t FFVPacketQueue::getSerial()
{
    return serial.load();
}

bool FFVPacketQueue::isFinished() const
{
    return impl->isFinished();
}

bool FFVPacketQueue::isClosed() const
{
    return impl->isClosed();
}

MediaQueueCloseReason FFVPacketQueue::closeReason() const
{
    return impl->closeReason();
}

int FFVPacketQueue::errorCode() const
{
    return impl->errorCode();
}

void FFVPacketQueue::close(MediaQueueCloseReason reason, int errorCode)
{
    qDebug() << "[FFVPacketQueue] close reason=" << mediaQueueCloseReasonName(reason)
             << "errorCode=" << errorCode;
    impl->close(reason, errorCode);
}

void FFVPacketQueue::start()
{
    impl->start();
}

int FFVPacketQueue::length()
{
    return impl->length();
}

void FFVPacketQueue::wakeAllThread()
{
    close(MediaQueueCloseReason::RequestedStop);
}

void FFVPacketQueue::clearQueue()
{
    impl->clearQueue();
}

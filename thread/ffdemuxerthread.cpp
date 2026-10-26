#include "ffdemuxerthread.h"

#include "demuxer/demuxer.h"
#include "recorder/ffrecorder_p.h"
#include "thread/wincomscope.h"
#include <QDebug>
#include <chrono>

using namespace FFRecordContextType;

FFDemuxerThread::FFDemuxerThread()
{
    stopFlag.store(true);
}

FFDemuxerThread::~FFDemuxerThread()
{
    demuxer = nullptr;
}

void FFDemuxerThread::init(Demuxer *demuxer_)
{
    std::lock_guard<std::mutex> lock(mutex);
    demuxer = demuxer_;
    prepareOpenRequested = false;
    prepareOpenReady = false;
    prepareOpenSuccess = false;
    waitForResume = false;
    resumeRequested = false;
}

bool FFDemuxerThread::prepareOpenAndWait(int timeoutMs,
                                          const std::function<bool()> &shouldOpen)
{
    if (shouldOpen && !shouldOpen()) return false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        openGuard = shouldOpen;
        prepareOpenRequested = true;
        prepareOpenReady = false;
        prepareOpenSuccess = false;
        waitForResume = true;
        resumeRequested = false;
    }

    start();

    std::unique_lock<std::mutex> lock(mutex);
    const bool ready = cond.wait_for(lock,
                                     std::chrono::milliseconds(timeoutMs),
                                     [this] {
                                         return prepareOpenReady || m_stop.load(std::memory_order_acquire);
                                     });
    if (!ready) {
        qDebug() << "[FFDemuxerThread] prepareOpenAndWait timeout";
        return false;
    }

    return prepareOpenReady && prepareOpenSuccess;
}

void FFDemuxerThread::resumePreparedDemux()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        waitForResume = false;
        resumeRequested = true;
    }
    cond.notify_all();
}

void FFDemuxerThread::wakeAllThread()
{
    requestAbort();
}

void FFDemuxerThread::requestDrainStop()
{
    if (demuxer) {
        demuxer->requestDrainStop();
    }
    m_stop.store(true, std::memory_order_release);
    cond.notify_all();
}

void FFDemuxerThread::requestAbort()
{
    if (demuxer) {
        demuxer->requestAbort();
    }
    m_stop.store(true, std::memory_order_release);
    cond.notify_all();
}

void FFDemuxerThread::close()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        waitForResume = false;
        resumeRequested = true;
        prepareOpenRequested = false;
    }
    if (demuxer) {
        demuxer->close();
    }

    cond.notify_all();
    stopFlag.store(true, std::memory_order_release);
}

bool FFDemuxerThread::peekStop()
{
    return m_stop.load(std::memory_order_acquire);
}

void FFDemuxerThread::run()
{
    ScopedWinComInitializer comInit("FFDemuxerThread");
    qDebug() << "[FFDemuxerThread] COM"
             << "tried=" << comInit.tried()
             << "ready=" << comInit.ready()
             << "hr=0x" << QString::number(static_cast<qulonglong>(static_cast<unsigned long>(comInit.result())), 16);
    bool doPrepareOpen = false;
    std::function<bool()> shouldOpen;
    {
        std::lock_guard<std::mutex> lock(mutex);
        doPrepareOpen = prepareOpenRequested;
        shouldOpen = std::move(openGuard);
    }
    if (demuxer && doPrepareOpen) {
        qDebug() << "[FFDemuxerThread] prepare open on demux thread";
        const bool opened = demuxer->openOnCurrentThread(shouldOpen);
        {
            std::lock_guard<std::mutex> lock(mutex);
            prepareOpenReady = true;
            prepareOpenSuccess = opened;
            prepareOpenRequested = false;
        }
        cond.notify_all();
        if (!opened) {
            qDebug() << "[FFDemuxerThread] prepare open failed";
            return;
        }

        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [this] {
            return resumeRequested || m_stop.load(std::memory_order_acquire);
        });
        resumeRequested = false;
        if (m_stop.load(std::memory_order_acquire)) {
            return;
        }
    } else if (demuxer && demuxer->getType() == SCREEN && !demuxer->isOpen()) {
        qDebug() << "[FFDemuxerThread] reopening screen demuxer on demux thread";
        if (!demuxer->reopenOnCurrentThread()) {
            qDebug() << "[FFDemuxerThread] reopen screen demuxer failed";
            return;
        }
    }
    while (!m_stop) {
        stopFlag.store(false, std::memory_order_release);
        int ret = demuxer->demux();
        if (ret != 0) {
            break;
        }
        if (m_stop) {
            break;
        }
    }
}

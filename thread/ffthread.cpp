#include "ffthread.h"

#include <QDebug>

FFThread::FFThread()
    : m_stop(true)
{}

FFThread::~FFThread()
{
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void FFThread::stop()
{
    m_stop.store(true, std::memory_order_release);
}

void FFThread::wait()
{
    bool flag = m_thread.joinable();
    if (flag) {
        m_thread.join();
    }
}

bool FFThread::waitFor(std::chrono::milliseconds timeout)
{
    if (!m_thread.joinable()) return true;
    std::unique_lock<std::mutex> lock(completionMutex);
    if (!completion.wait_for(lock, timeout, [this] { return finished; })) return false;
    lock.unlock();
    m_thread.join();
    return true;
}

void FFThread::start()
{
    bool expected = true;
    if (!m_stop.compare_exchange_strong(expected, false)) {
        return;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    {
        std::lock_guard<std::mutex> lock(completionMutex);
        finished = false;
    }
    m_thread = std::thread([this] {
        this->run();
        {
            std::lock_guard<std::mutex> lock(completionMutex);
            m_stop.store(true, std::memory_order_release);
            finished = true;
        }
        completion.notify_all();
    });
}

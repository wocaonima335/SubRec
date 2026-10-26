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

void FFThread::start()
{
    bool expected = true;
    if (!m_stop.compare_exchange_strong(expected, false)) {
        return;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_thread = std::thread([this] {
        this->run();
        m_stop.store(true, std::memory_order_release);
    });
}

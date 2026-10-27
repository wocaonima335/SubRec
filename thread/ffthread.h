#ifndef FFTHREAD_H
#define FFTHREAD_H

#include <atomic>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>

class FFThread
{
public:
    explicit FFThread();

    virtual void run() = 0;
    virtual ~FFThread();
    void stop();
    void wait();
    bool waitFor(std::chrono::milliseconds timeout);
    void start();

protected:
    std::atomic<bool> m_stop;

private:
    std::thread m_thread;
    std::mutex completionMutex;
    std::condition_variable completion;
    bool finished = true;
};

#endif // FFTHREAD_H

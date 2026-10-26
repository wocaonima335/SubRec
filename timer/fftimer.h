#ifndef FFTIMER_H
#define FFTIMER_H

#include <QMetaObject>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iomanip>
#include <iostream>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/time.h>
}

class FFVFrameQueue;
class FFVRender;
class FFVideoPars;
class FFCapWindow;

class FFTimer
{
public:
    FFTimer();
    ~FFTimer();

    void init(FFVFrameQueue *frmQueue_, FFVRender *vRender_, FFCapWindow *capWindow_);
    void start();
    void wait();
    void stop();
    void pause();
    void close();
    void wakeAllThread();
    void setSpeed(float speed_);

    bool peekReadyFlag();

private:
    void work();
    void copyYUV(AVFrame *frame);
    void setTimerInterval(std::chrono::milliseconds interval_);
    void setTimerInterval(double interval_);
    void setTimerInterval(int64_t interval_);

    void playVideo();

private:
    std::chrono::microseconds interval;

    std::thread timerTread;
    bool m_stop;

    FFVFrameQueue *frmQueue = nullptr;
    FFVideoPars *vPars = nullptr;
    FFVRender *vRender = nullptr;

    std::mutex mutex;
    std::condition_variable cond;
    std::condition_variable pauseCond;
    std::mutex pauseMutex;

    std::atomic<bool> seekFlag;
    std::atomic<bool> pauseFlag;
    uint8_t *yBuf = nullptr;
    uint8_t *uBuf = nullptr;
    uint8_t *vBuf = nullptr;

    std::atomic<bool> speedFlag;
    float speed;
    float speedFactor;

    FFCapWindow *capWindow = nullptr;
    std::atomic<bool> readyFlag;
};

#endif // FFTIMER_H

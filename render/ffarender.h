#ifndef FFARENDER_H
#define FFARENDER_H

#include "decoder/ffadecoder.h"

extern "C" {
#include "libavformat/avformat.h"
#include "libavutil/samplefmt.h"
}

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QIODevice>

#include <atomic>
#include <condition_variable>

struct sonicStreamStruct;
class FFADecoder;
class FFAFrameQueue;
class FFPlayerWindow;
class FFPlayerContext;

class FFARender
{
public:
    FFARender();
    virtual ~FFARender();

    void init(FFAFrameQueue *frmQueue_, FFADecoder *aDecoder_, FFPlayerWindow *playerWindow_);
    void stop();
    void pause();
    bool peekReady();
    void wakeAllThread();
    void seek();
    void close();
    void setSpeed(float speed_);
    void setVolume(double volume);

    // 将原来的 protected run() 改为 public render()
    void render();

private:
    void initAudio();
    void initAudioPars();
    void playAudio(AVFrame *frame);
    void sendEndEvent();
    void sendProcessEvent(int curSeconds);

private:
    FFAFrameQueue *frmQueue = nullptr;
    FFADecoder *aDecoder = nullptr;
    FFPlayerWindow *playerWindow = nullptr;
    QAudioOutput *aOutput = nullptr;
    QAudioFormat aFormat;
    QIODevice *aDevice;
    QAudioDevice deviceInfo;

    FFAudioPars *aPars = nullptr;
    uint8_t *abuf = nullptr;
    int64_t maxBufSize = -1;

    int clockBase = 0;
    std::atomic<bool> readyFlag;
    int totalSec = 0;
    FFPlayerContext *playerCtx = nullptr;
    int lastSec = -1;

    std::atomic<bool> seekFlag;
    std::atomic<bool> pauseFlag;

    std::condition_variable pauseCond;
    std::mutex mutex;

    sonicStreamStruct *sonicCtx = nullptr;
    std::atomic<bool> speedFlag;
    float speed;

    uint8_t *abufOut = nullptr;
    unsigned int abufOutSize = 0;
    unsigned int maxaBufOut = -1;
};

#endif // FFARENDER_H

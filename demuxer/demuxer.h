#ifndef DEMUXER_H
#define DEMUXER_H

#include <QObject>
#include <QString>
#include <atomic>
#include <mutex>
#include <functional>
#include "capture/capture_types.h"

using namespace std;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

class FFVPacketQueue;
class FFAPacketQueue;

class Demuxer
{
public:
    explicit Demuxer();
    Demuxer(const Demuxer &) = delete;
    Demuxer &operator=(const Demuxer &) = delete;
    ~Demuxer();

    void init(string const &url,
              string const &format_,
              FFAPacketQueue *aPktQueue_,
              FFVPacketQueue *vPktQueue_,
              int type,
              bool openNow = true,
              const std::function<bool()> &shouldOpen = {});
    int demux();
    AVStream *getAStream();
    AVStream *getVStream();
    bool isOpen();
    bool openOnCurrentThread(const std::function<bool()> &shouldOpen = {});
    CaptureStartResult openResult(const InputRequest &input);
    bool reopenOnCurrentThread();
    void requestDrainStop();
    void requestAbort();
    void wakeAllThread();
    void close();
    void initDemuxer(const std::function<bool()> &shouldOpen = {});
    int getType();
    void PrintDshowDevices();

private:
    void printError(int ret);
    void failOpen(CaptureErrorStage stage, int code);
    static int interruptOpen(void *opaque);
    std::function<bool()> openGuard;

private:
    string url;
    string format;

    AVFormatContext *fmtCtx = nullptr;
    AVDictionary *opts = nullptr;
    AVStream *aStream = nullptr;
    AVStream *vStream = nullptr;

    FFVPacketQueue *vPktQueue = nullptr;
    FFAPacketQueue *aPktQueue = nullptr;

    AVRational aTimeBase;
    AVRational vTimeBase;

    const AVInputFormat *inputFmt = nullptr;
    int type = 0;
    CaptureErrorStage failureStage = CaptureErrorStage::None;
    int failureCode = 0;
    bool offsetFlag = false;

    std::mutex mutex;
    std::atomic<bool> stopFlag{false};
    std::atomic<bool> drainRequested{false};
};

#endif // DEMUXER_H

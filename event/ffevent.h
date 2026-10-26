#ifndef FFEVENT_H
#define FFEVENT_H
#include "decoder/ffadecoder.h"
#include "decoder/ffvdecoder.h"
#include "demuxer/demuxer.h"

#include "filter/ffafilter.h"
#include "filter/ffvfilter.h"

#include "muxer/ffmuxer.h"

#include "queue/ffaframequeue.h"
#include "queue/ffapacketqueue.h"
#include "queue/ffvframequeue.h"
#include "queue/ffvpacketqueue.h"

#include "recorder/ffrecorder.h"

#include <QMetaObject>

class FFAFilterThread;
class FFVFilterThread;
class FFAEncoderThread;
class FFVEncoderThread;
class FFMuxerThread;
class FFADecoderThread;
class FFVDecoderThread;
class FFDemuxerThread;
class FFDemuxerThread;

class FFEvent
{
public:
    FFEvent(FFRecorder *recoderContext_);
    virtual ~FFEvent();
    virtual void work() = 0;

    // 返回true表示事件需要串行执行（避免资源竞态）
    virtual bool requiresSerialization() const { return false; }

protected:
    FFRecorder *recoderContext = nullptr; // 全局上下文

    FFAPacketQueue *aPktQueue[A_DEMUXER_SIZE] = {}; // 音频包队列数组
    FFVPacketQueue *vPktQueue[V_DEMUXER_SIZE] = {}; // 视频包队列数组
    FFAPacketQueue *aEncoderPktQueue = nullptr;     // 音频编码包队列
    FFVPacketQueue *vEncoderPktQueue = nullptr;     // 视频编码包队列

    FFAFrameQueue *aFrmQueue[A_DECODER_SIZE] = {}; // 音频帧队列数组
    FFVFrameQueue *vFrmQueue[V_DECODER_SIZE] = {}; // 视频帧队列数组
    FFAFrameQueue *aFilterEncoderFrmQueue = nullptr;    // 音频滤镜编码帧队列
    FFVFrameQueue *vFilterEncoderFrmQueue = nullptr;    // 视频滤镜编码帧队列

    Demuxer *aDemuxer[A_DEMUXER_SIZE] = {}; // 音频解复用器数组
    Demuxer *vDemuxer[V_DEMUXER_SIZE] = {}; // 视频解复用器数组

    FFADecoder *aDecoder[A_DECODER_SIZE] = {}; // 音频解码器数组
    FFVDecoder *vDecoder[V_DECODER_SIZE] = {}; // 视频解码器数组

    FFAFilter *aFilter = nullptr; // 音频混合过滤器
    FFVFilter *vFilter = nullptr; // 视频过滤器

    FFAEncoder *aEncoder = nullptr; // 音频编码器
    FFVEncoder *vEncoder = nullptr; // 视频编码器

    FFMuxer *muxer = nullptr; // 复用器

    FFDemuxerThread *aDemuxerThread[A_DEMUXER_SIZE] = {}; // 音频解复用线程数组
    FFDemuxerThread *vDemuxerThread[V_DEMUXER_SIZE] = {}; // 视频解复用线程数组

    FFADecoderThread *aDecoderThread[A_DECODER_SIZE] = {}; // 音频解码线程数组
    FFVDecoderThread *vDecoderThread[V_DECODER_SIZE] = {}; // 视频解码线程数组

    FFAFilterThread *aFilterThread = nullptr; // 音频过滤线程
    FFVFilterThread *vFilterThread = nullptr; // 视频过滤线程

    FFAEncoderThread *aEncoderThread = nullptr; // 音频编码线程
    FFVEncoderThread *vEncoderThread = nullptr; // 视频编码线程

    FFMuxerThread *muxerThread = nullptr; // 复用线程
};

#endif // FFEVENT_H

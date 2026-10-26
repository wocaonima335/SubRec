#ifndef FFRECORDER_P_H
#define FFRECORDER_P_H

#include "ffaudiosampler.h"

#include "decoder/ffadecoder.h"
#include "decoder/ffvdecoder.h"
#include "demuxer/demuxer.h"
#include "encoder/ffaencoder.h"
#include "encoder/ffvencoder.h"
#include "filter/ffafilter.h"
#include "filter/ffvfilter.h"
#include "muxer/ffmuxer.h"

#include "queue/ffaframequeue.h"
#include "queue/ffapacketqueue.h"
#include "queue/recordersyncqueue.h"
#include "queue/ffvframequeue.h"
#include "queue/ffvpacketqueue.h"

#include "thread/ffadecoderthread.h"
#include "thread/ffaencoderthread.h"
#include "thread/ffafilterthread.h"
#include "thread/ffdemuxerthread.h"
#include "thread/ffmuxerthread.h"
#include "thread/ffscreengrabthread.h"
#include "thread/ffvdecoderthread.h"
#include "thread/ffvencoderthread.h"
#include "thread/ffvfilterthread.h"

#include <QObject>
#include <QtWidgets/QApplication>

namespace FFRecordURLS
{
    static std::string CAMERA_URL = "video=Integrated Camera";
    static std::string SCREEN_URL = "video=screen-capture-recorder";
    static std::string AUDIO_URL = "audio=virtual-audio-capturer";
    static std::string MICROPHONE_URL = "audio=麦克风阵列 (Realtek(R) Audio)";
    }; // namespace FFRecordURLS

    namespace FFRecordContextType {
    constexpr size_t A_DECODER_SIZE = 2;
    constexpr size_t A_DEMUXER_SIZE = 2;

    constexpr size_t V_DECODER_SIZE = 3;
    constexpr size_t V_DEMUXER_SIZE = 3;

    enum demuxerType
    {
        SCREEN,
        CAMERA,
        VIDEO,
        AUDIO,
        MICROPHONE,
        NOTYPE
    };

    enum aDecoderType
    {
        A_MICROPHONE,  // 0 - 对应 demuxerIndex[MICROPHONE]=0
        A_AUDIO        // 1 - 对应 demuxerIndex[AUDIO]=1
    };
    enum vDecoderType
    {
        V_SCREEN,      // 0 - 对应 demuxerIndex[SCREEN]=0
        V_CAMERA,      // 1 - 对应 demuxerIndex[CAMERA]=1
        V_VIDEO        // 2 - 对应 demuxerIndex[VIDEO]=2
    };

    constexpr int demuxerIndex[A_DEMUXER_SIZE + V_DEMUXER_SIZE + 1]{0, 1, 2, 1, 0, -1};

    class FFRecorderPrivate
    {
    public:
        FFRecorderPrivate();
        ~FFRecorderPrivate();

        FFADecoderThread *aDecoderThread[FFRecordContextType::A_DECODER_SIZE];
        FFDemuxerThread *aDemuxerThread[FFRecordContextType::A_DEMUXER_SIZE];

        FFVDecoderThread *vDecoderThread[FFRecordContextType::V_DECODER_SIZE];
        FFDemuxerThread *vDemuxerThread[FFRecordContextType::V_DEMUXER_SIZE];

        // 音频解码packet包队列
        FFAPacketQueue *aDecoderPktQueue[FFRecordContextType::A_DECODER_SIZE];

        // 视频解码packet包队列
        FFVPacketQueue *vDecoderPktQueue[FFRecordContextType::V_DECODER_SIZE];

        // 视频编码packet包队列
        FFVPacketQueue *vEncoderPktQueue;

        // 音频编码packet包队列
        FFAPacketQueue *aEncoderPktQueue;

        // 音频解码帧队列
        FFAFrameQueue *aDecoderFrmQueue[FFRecordContextType::A_DECODER_SIZE];

        // 视频解码帧队列
        FFVFrameQueue *vDecoderFrmQueue[FFRecordContextType::V_DECODER_SIZE];

        // 视频filter编码Frame队列
        FFVFrameQueue *vFilterEncoderFrmQueue;

        // 音频filter编码Frame队列
        FFAFrameQueue *aFilterEncoderFrmQueue;

        // A/V 同步模块
        RecorderSyncQueue *syncQueue;

        // 音频解复用器
        Demuxer *aDemuxer[FFRecordContextType::A_DEMUXER_SIZE];

        // 视频解复用器
        Demuxer *vDemuxer[FFRecordContextType::V_DEMUXER_SIZE];

        // 音频解码器
        FFADecoder *aDecoder[FFRecordContextType::A_DECODER_SIZE];

        // 视频解码器
        FFVDecoder *vDecoder[FFRecordContextType::V_DECODER_SIZE];

        // 复用器
        FFMuxer *muxer;

        // 复用线程
        class FFMuxerThread *muxerThread;

        // 音频编码器
        FFAEncoder *aEncoder;

        // 音频编码线程
        class FFAEncoderThread *aEncoderThread;

        // 视频编码器
        FFVEncoder *vEncoder;

        // 视频编码线程
        FFVEncoderThread *vEncoderThread;

        // 视频过滤器
        FFVFilter *vFilter;

        // 视频过滤线程
        FFVFilterThread *vFilterThread;

        // ddagrab 屏幕帧源（替代屏幕的 demuxer + decoder，帧写入 vDecoderFrmQueue[SCREEN]）
        FFScreenGrabThread *screenGrabThread;

        // 音频过滤器
        FFAFilter *aFilter;

        // 音频滤镜线程
        FFAFilterThread *aFilterThread;

        //音频采集器
        FFAudioSampler *audioSampler;
    };

    } // namespace FFRecordContextType

#endif // FFRECORDER_P_H

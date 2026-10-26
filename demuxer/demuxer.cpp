#include "demuxer.h"
extern "C" {
#include <libavutil/time.h>
}

#include "queue/ffapacketqueue.h"
#include "queue/ffvpacketqueue.h"
#include "recorder/ffrecorder_p.h"

#include <QCryptographicHash>
#include <QGuiApplication>
#include <QScreen>
#include <QStringList>
#include <QThread>
#include <algorithm>
#include <iostream>

using namespace FFRecordContextType;

namespace {
std::atomic<int> g_videoPacketMd5LogCount{0};

quint64 packetFingerprint(const AVPacket *packet, int maxSamples = 128)
{
    static constexpr quint64 kOffset = 1469598103934665603ull;
    static constexpr quint64 kPrime = 1099511628211ull;

    if (!packet || !packet->data || packet->size <= 0) {
        return 0;
    }

    quint64 hash = kOffset;
    const int step = std::max(1, packet->size / maxSamples);
    int samples = 0;
    for (int index = 0; index < packet->size && samples < maxSamples; index += step) {
        hash ^= packet->data[index];
        hash *= kPrime;
        ++samples;
    }

    hash ^= packet->data[packet->size - 1];
    hash *= kPrime;
    return hash;
}

QByteArray packetMd5(const AVPacket *packet)
{
    if (!packet || !packet->data || packet->size <= 0) {
        return {};
    }

    return QCryptographicHash::hash(
               QByteArray(reinterpret_cast<const char *>(packet->data), packet->size),
               QCryptographicHash::Md5)
        .toHex();
}

QString dumpDictEntries(AVDictionary *dict)
{
    if (!dict) {
        return QStringLiteral("<none>");
    }

    QStringList entries;
    AVDictionaryEntry *entry = nullptr;
    while ((entry = av_dict_get(dict, "", entry, AV_DICT_IGNORE_SUFFIX)) != nullptr) {
        entries << QStringLiteral("%1=%2")
                       .arg(QString::fromUtf8(entry->key), QString::fromUtf8(entry->value));
    }

    return entries.isEmpty() ? QStringLiteral("<consumed>") : entries.join(QStringLiteral(", "));
}
}

Demuxer::Demuxer() {}

Demuxer::~Demuxer() {}

void Demuxer::init(const string &url_,
                   const string &format_,
                   FFAPacketQueue *aPktQueue_,
                   FFVPacketQueue *vPktQueue_,
                   int type_,
                   bool openNow,
                   const std::function<bool()> &shouldOpen)
{
    std::lock_guard<std::mutex> lock(mutex);
    url = url_;
    format = format_;
    aPktQueue = aPktQueue_;
    vPktQueue = vPktQueue_;
    type = type_;
    if (type_ == SCREEN || type_ == CAMERA || type_ == VIDEO) {
        g_videoPacketMd5LogCount.store(0, std::memory_order_release);
    }
    stopFlag.store(false, std::memory_order_release);
    drainRequested.store(false, std::memory_order_release);
    if (fmtCtx) {
        avformat_close_input(&fmtCtx);
        fmtCtx = nullptr;
    }
    aStream = nullptr;
    vStream = nullptr;
    aTimeBase = AVRational{0, 1};
    vTimeBase = AVRational{0, 1};
    if (openNow) {
        initDemuxer(shouldOpen);
    }
}

int Demuxer::demux()
{
    std::lock_guard<std::mutex> lock(mutex);
    AVPacket *packet = av_packet_alloc();

    if (fmtCtx == nullptr) {
        if (drainRequested.load(std::memory_order_acquire)) {
            qDebug() << "[Demux] drain requested with null fmtCtx, return EOF";
            drainRequested.store(false, std::memory_order_release);
            return 1;
        }
        qDebug() << "[Demux] fmtCtx is null, return -1";
        return -1;
    }

    int ret = av_read_frame(fmtCtx, packet);

    if (ret < 0) {
        if (ret == AVERROR_EOF) {
            if (aPktQueue) {
                aPktQueue->sendFinish();
            }

            if (vPktQueue) {
                vPktQueue->sendFinish();
            }
            av_packet_unref(packet);
            av_packet_free(&packet);
            std::cout << "AVERROR_EOF" << endl;
            qDebug() << "[Demux] EOF, sendFinish A/V, return 1";
            return 1;
        } else if (ret == AVERROR(EAGAIN)) {
            av_packet_free(&packet);
            return 0;
        } else {
            if (stopFlag.load(std::memory_order_acquire)
                && drainRequested.load(std::memory_order_acquire)) {
                if (aPktQueue) {
                    aPktQueue->sendFinish();
                }
                if (vPktQueue) {
                    vPktQueue->sendFinish();
                }
                av_packet_unref(packet);
                av_packet_free(&packet);
                qDebug() << "[Demux] drain stop after read error, return EOF";
                drainRequested.store(false, std::memory_order_release);
                return 1;
            }
            char err[256] = {0};
            av_strerror(ret, err, sizeof(err));
            qDebug() << "[Demux][ERR] ret=" << ret << " msg=" << err << " -> close fmtCtx";
            if (aPktQueue) {
                aPktQueue->close(MediaQueueCloseReason::Error, ret);
            }
            if (vPktQueue) {
                vPktQueue->close(MediaQueueCloseReason::Error, ret);
            }
            avformat_close_input(&fmtCtx);
            av_packet_free(&packet);
            return -1;
        }
    }

    if (stopFlag.load(std::memory_order_acquire)) {
        qDebug() << "[Demux] stopFlag set, return 0";
        av_packet_unref(packet);
        av_packet_free(&packet);
        return 0;
    }

    if (aStream && packet->stream_index == aStream->index) {
        if (captureClock) {
            // Calibrate at the first demuxed packet, before video preparation and
            // downstream queueing. Thereafter preserve the device PTS deltas,
            // including pauses/gaps and rate drift; never re-anchor queued audio.
            const auto *par = aStream->codecpar;
            const int bytes = av_get_bytes_per_sample(static_cast<AVSampleFormat>(par->format));
            const int64_t durationUs = bytes > 0 && par->ch_layout.nb_channels > 0 && par->sample_rate > 0
                ? int64_t(packet->size) * 1000000 / (bytes * par->ch_layout.nb_channels) / par->sample_rate : 0;
            const int64_t deviceUs = packet->pts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE
                : av_rescale_q(packet->pts, aStream->time_base, AVRational{1,1000000});
            if (captureOffsetUs == AV_NOPTS_VALUE && deviceUs != AV_NOPTS_VALUE)
                captureOffsetUs = av_gettime_relative() - durationUs - deviceUs;
            const int64_t captureUs = deviceUs == AV_NOPTS_VALUE ? av_gettime_relative() - durationUs
                : deviceUs + captureOffsetUs;
            packet->pts = packet->dts = av_rescale_q(captureUs, AVRational{1,1000000}, aStream->time_base);
        }
        if (aPktQueue) {
            aPktQueue->enqueue(packet);
            av_packet_free(&packet);
        } else {
            av_packet_unref(packet);
            av_packet_free(&packet);
        }
    } else if (vStream && packet->stream_index == vStream->index) {
        qDebug() << "[DemuxHash] type=" << type
                 << " stream=" << packet->stream_index
                 << " pts=" << packet->pts
                 << " dts=" << packet->dts
                 << " size=" << packet->size
                 << " flags=0x" << Qt::hex << packet->flags << Qt::dec
                 << " hash=0x" << QString::number(packetFingerprint(packet), 16);
        const int packetLogIndex =
            g_videoPacketMd5LogCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (packetLogIndex <= 60) {
            qDebug().noquote()
                << "[DemuxPacketMd5]"
                << "index=" << packetLogIndex
                << "pts=" << packet->pts
                << "size=" << packet->size
                << "md5=" << packetMd5(packet);
        }
        if (vPktQueue) {
            vPktQueue->enqueue(packet);
            av_packet_free(&packet);
        } else {
            av_packet_unref(packet);
            av_packet_free(&packet);
        }
    } else {
        av_packet_unref(packet);
        av_packet_free(&packet);
    }
    return 0;
}

AVStream *Demuxer::getAStream()
{
    std::lock_guard<std::mutex> lock(mutex);
    return aStream;
}

AVStream *Demuxer::getVStream()
{
    std::lock_guard<std::mutex> lock(mutex);
    return vStream;
}

bool Demuxer::isOpen()
{
    std::lock_guard<std::mutex> lock(mutex);
    return fmtCtx != nullptr;
}

bool Demuxer::openOnCurrentThread(const std::function<bool()> &shouldOpen)
{
    std::lock_guard<std::mutex> lock(mutex);
    stopFlag.store(false, std::memory_order_release);
    drainRequested.store(false, std::memory_order_release);
    if (fmtCtx) {
        return true;
    }
    initDemuxer(shouldOpen);
    return fmtCtx != nullptr;
}

bool Demuxer::reopenOnCurrentThread()
{
    std::lock_guard<std::mutex> lock(mutex);
    stopFlag.store(false, std::memory_order_release);
    drainRequested.store(false, std::memory_order_release);
    if (fmtCtx) {
        avformat_close_input(&fmtCtx);
        fmtCtx = nullptr;
    }
    aStream = nullptr;
    vStream = nullptr;
    aTimeBase = AVRational{0, 1};
    vTimeBase = AVRational{0, 1};
    initDemuxer();
    return fmtCtx != nullptr;
}

void Demuxer::requestDrainStop()
{
    stopFlag.store(true, std::memory_order_release);
    drainRequested.store(true, std::memory_order_release);
    if (aPktQueue) {
        aPktQueue->sendFinish();
    }
    if (vPktQueue) {
        vPktQueue->sendFinish();
    }
}

void Demuxer::requestAbort()
{
    stopFlag.store(true, std::memory_order_release);
    drainRequested.store(false, std::memory_order_release);
    if (aPktQueue) {
        aPktQueue->close(MediaQueueCloseReason::RequestedStop, 0);
    }
    if (vPktQueue) {
        vPktQueue->close(MediaQueueCloseReason::RequestedStop, 0);
    }
}

void Demuxer::wakeAllThread()
{
    requestAbort();
}

void Demuxer::close()
{
    std::lock_guard<std::mutex> lock(mutex);
    stopFlag.store(true, std::memory_order_release);
    drainRequested.store(false, std::memory_order_release);
    if (fmtCtx) {
        avformat_close_input(&fmtCtx);
        fmtCtx = nullptr;
    }

    aStream = nullptr;
    vStream = nullptr;
    aTimeBase = AVRational{0, 1};
    vTimeBase = AVRational{0, 1};
    if (opts) {
        av_dict_free(&opts);
    }
}

void Demuxer::failOpen(CaptureErrorStage stage, int code)
{
    failureStage = stage;
    failureCode = code;
    avformat_close_input(&fmtCtx);
    aStream = nullptr;
    vStream = nullptr;
    aTimeBase = AVRational{0, 1};
    vTimeBase = AVRational{0, 1};
}

CaptureStartResult Demuxer::openResult(const InputRequest &input)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (fmtCtx && failureStage == CaptureErrorStage::None)
        return CaptureStartResult::success();
    CaptureError error;
    error.stage = failureStage == CaptureErrorStage::None
                      ? CaptureErrorStage::InputOpen : failureStage;
    error.role = input.kind;
    error.backend = input.backend;
    error.deviceId = input.deviceId;
    error.deviceName = input.displayName;
    error.ffmpegError = failureCode ? failureCode : AVERROR(EINVAL);
    char message[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(error.ffmpegError, message, sizeof(message));
    error.ffmpegMessage = QString::fromUtf8(message);
    error.userMessage = error.ffmpegError == AVERROR_EXIT
                            ? QStringLiteral("Capture start was cancelled.")
                            : QStringLiteral("Unable to prepare the selected capture input.");
    error.remediation = QStringLiteral("Refresh devices and verify the selected input and format.");
    return CaptureStartResult::failure(std::move(error));
}

int Demuxer::interruptOpen(void *opaque)
{
    auto *self = static_cast<Demuxer *>(opaque);
    return self->stopFlag.load(std::memory_order_acquire)
           || (self->openGuard && !self->openGuard());
}

void Demuxer::initDemuxer(const std::function<bool()> &shouldOpen)
{
    openGuard = shouldOpen;
    failureStage = CaptureErrorStage::None;
    failureCode = 0;
    // Cooperative boundaries only: a third-party device open may not be interruptible.
    const auto cancelled = [&] {
        if (stopFlag.load(std::memory_order_acquire) || (shouldOpen && !shouldOpen())) {
            failOpen(CaptureErrorStage::Configuration, AVERROR_EXIT);
            return true;
        }
        return false;
    };
    if (cancelled()) return;
    qDebug() << "[Demux] initDemuxer"
             << "thread=" << QThread::currentThreadId()
             << "type=" << type
             << "url=" << QString::fromStdString(url)
             << "format=" << QString::fromStdString(format);
    avformat_network_init();
    avdevice_register_all();

    inputFmt = av_find_input_format(format.c_str());

    if (!inputFmt) {
        qDebug() << "Cannot find input format: " << format;
        failOpen(CaptureErrorStage::InputFormat, AVERROR_DEMUXER_NOT_FOUND);
        return;
    }

    AVDictionary *localOpts = nullptr;

    if (type == AUDIO || type == MICROPHONE) {
        av_dict_set(&localOpts, "audio_buffer_size", "50", 0);
        qDebug() << "[Demux] Audio device, set audio_buffer_size=50ms";
    } else if (type == SCREEN) {
        av_dict_set(&localOpts, "framerate", "30", 0);
        av_dict_set(&localOpts, "pixel_format", "bgr0", 0);
        av_dict_set(&localOpts, "use_video_device_timestamps", "false", 0);
        av_dict_set(&localOpts, "rtbufsize", "256M", 0);
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            const QSize size = screen->size();
            if (size.width() > 0 && size.height() > 0) {
                const QByteArray videoSize =
                    QString("%1x%2").arg(size.width()).arg(size.height()).toLatin1();
                av_dict_set(&localOpts, "video_size", videoSize.constData(), 0);
                qDebug() << "[Demux] Screen device, set video_size=" << videoSize;
            }
        }
        qDebug() << "[Demux] Screen device opts:"
                 << "framerate=30 pixel_format=bgr0 use_video_device_timestamps=false rtbufsize=256M";
    } else {
        av_dict_set(&localOpts, "framerate", "30", 0);
    }

    if (cancelled()) {
        av_dict_free(&localOpts);
        return;
    }
    fmtCtx = avformat_alloc_context();
    if (!fmtCtx) {
        av_dict_free(&localOpts);
        failOpen(CaptureErrorStage::InputOpen, AVERROR(ENOMEM));
        return;
    }
    fmtCtx->interrupt_callback = {&Demuxer::interruptOpen, this};
    int ret = avformat_open_input(&fmtCtx, url.c_str(), inputFmt, &localOpts);

    if (ret < 0) {
        qDebug() << "[Demux] open_input failed, remaining opts:" << dumpDictEntries(localOpts);
        av_dict_free(&localOpts);
        failOpen(CaptureErrorStage::InputOpen, ret);
        printError(ret);
        return;
    }

    qDebug() << "[Demux] open_input success, remaining opts:" << dumpDictEntries(localOpts);
    av_dict_free(&localOpts);

    if (cancelled()) return;
    // DirectShow describes raw PCM in read_header. Probing it can pre-buffer
    // seconds of samples before the first packet used for clock calibration.
    bool describedPcm = false;
    if (captureClock && format == "dshow") {
        for (unsigned i = 0; i < fmtCtx->nb_streams; ++i) {
            const auto *par = fmtCtx->streams[i]->codecpar;
            if (par->codec_type == AVMEDIA_TYPE_AUDIO && par->sample_rate > 0 &&
                par->ch_layout.nb_channels > 0 && av_get_bytes_per_sample(static_cast<AVSampleFormat>(par->format)) > 0)
                describedPcm = true;
        }
    }
    ret = describedPcm ? 0 : avformat_find_stream_info(fmtCtx, nullptr);
    if (ret < 0) {
        failOpen(CaptureErrorStage::StreamInfo, ret);
        printError(ret);
        return;
    }

    if (cancelled()) return;
    for (size_t i = 0; i < fmtCtx->nb_streams; ++i) {
        AVStream *stream = fmtCtx->streams[i];
        if (!stream || !stream->codecpar) continue;
        AVCodecParameters *codecPar = stream->codecpar;

        if (codecPar->codec_type == AVMEDIA_TYPE_AUDIO) {
            aStream = stream;
            aTimeBase = stream->time_base;
        } else if (codecPar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vStream = stream;
            vTimeBase = stream->time_base;
            qDebug() << "[Demux] Video stream info"
                     << "codec=" << avcodec_get_name(codecPar->codec_id)
                     << "size=" << codecPar->width << "x" << codecPar->height
                     << "pixFmt=" << av_get_pix_fmt_name(static_cast<AVPixelFormat>(codecPar->format))
                     << "tb=" << stream->time_base.num << "/" << stream->time_base.den
                     << "avg=" << stream->avg_frame_rate.num << "/" << stream->avg_frame_rate.den
                     << "r=" << stream->r_frame_rate.num << "/" << stream->r_frame_rate.den;
        }
    }
    const bool wantsAudio = type == AUDIO || type == MICROPHONE;
    if ((wantsAudio && !aStream) || (!wantsAudio && !vStream)) {
        failOpen(CaptureErrorStage::StreamInfo, AVERROR_STREAM_NOT_FOUND);
    }
}

int Demuxer::getType()
{
    return type;
}

void Demuxer::PrintDshowDevices()
{
    const AVInputFormat *fmt = av_find_input_format("gdigrab");
    if (!fmt) {
        qDebug() << "gdigrab input format not found, your FFmpeg may not include gdigrab.";
        return;
    }

    AVDeviceInfoList *list = nullptr;
    int count = avdevice_list_input_sources(const_cast<AVInputFormat *>(fmt),
                                            nullptr,
                                            nullptr,
                                            &list);

    if (count > 0 && list) {
        std::cout << "Found " << list->nb_devices << " gdigrab sources:";
        for (int i = 0; i < list->nb_devices; ++i) {
            AVDeviceInfo *info = list->devices[i];
            const char *name = (info && info->device_name) ? info->device_name : "";
            const char *desc = (info && info->device_description) ? info->device_description : "";
            std::cout << "  [" << i << "] name=" << name << " | desc=" << desc;
        }
        avdevice_free_list_devices(&list);
        return;
    }
    avdevice_free_list_devices(&list);

    AVFormatContext *tmpCtx = nullptr;
    AVDictionary *listOpts = nullptr;
    av_dict_set(&listOpts, "list_windows", "1", 0);
    av_log_set_level(AV_LOG_INFO);

    int ret = avformat_open_input(&tmpCtx, "desktop", fmt, &listOpts);
    if (ret < 0) {
        char err[256] = {0};
        av_strerror(ret, err, sizeof(err));
        qDebug() << "gdigrab list_windows failed: " << err;
    } else {
        avformat_close_input(&tmpCtx);
    }
    av_dict_free(&listOpts);
}

void Demuxer::printError(int ret)
{
    char errorBuffer[AV_ERROR_MAX_STRING_SIZE];
    int res = av_strerror(ret, errorBuffer, sizeof errorBuffer);
    if (res < 0) {
        qDebug() << "Unknow Error!";
    } else {
        qDebug() << "Error:" << errorBuffer;
    }
}

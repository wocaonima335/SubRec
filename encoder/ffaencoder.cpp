#include "logging/frame_log.h"
#include "ffaencoder.h"
#include "audio_encoding_config.h"

#include "queue/ffapacketqueue.h"

#include <iostream>

FFAEncoder::FFAEncoder() {}

FFAEncoder::~FFAEncoder()
{
    close();
}

void FFAEncoder::init(FFAPacketQueue *pktQueue_)
{
    pktQueue = pktQueue_;
}

void FFAEncoder::close()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (codecCtx) {
        avcodec_free_context(&codecCtx);
        codecCtx = nullptr;
    }

    if (aPars) {
        delete aPars;
        aPars = nullptr;
    }

    pendingFrame.samples = 0;
    pendingFrame.next_pts = AV_NOPTS_VALUE;
    for (int ch = 0; ch < 8; ++ch) {
        pendingFrame.data[ch].clear();
        pendingFrame.data[ch].shrink_to_fit();
    }
    qDebug() << "[AEnc] close";
}

void FFAEncoder::wakeAllThread()
{
    requestAbort();
}

void FFAEncoder::requestDrainStop()
{
    qDebug() << "[AEnc] requestDrainStop";
}

void FFAEncoder::requestAbort()
{
    if (pktQueue) {
        pktQueue->close(MediaQueueCloseReason::RequestedStop);
    }
}

int FFAEncoder::encode(AVFrame *frame, int streamIndex, int64_t pts, AVRational timeBase)
{
    std::lock_guard<std::mutex> lock(mutex);
    Q_UNUSED(timeBase);

    if (codecCtx == nullptr) {
        return 0;
    }

    if (frame == nullptr) {
        int ret_send = avcodec_send_frame(codecCtx, nullptr);
        if (ret_send < 0 && ret_send != AVERROR_EOF) {
            printError(ret_send);
            return -1;
        }

        while (true) {
            AVPacket *pkt = av_packet_alloc();
            int ret = avcodec_receive_packet(codecCtx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                av_packet_free(&pkt);
                break;
            }
            if (ret < 0) {
                av_packet_free(&pkt);
                printError(ret);
                return -1;
            }

            pkt->stream_index = streamIndex;
            pktQueue->enqueue(pkt);
        }
        return 0;
    }

    qCDebug(subrec::logging::frameLog) << "[AEnc] encode enter: in_nb_samples=" << frame->nb_samples
             << " ch=" << codecCtx->ch_layout.nb_channels << " frame_size=" << codecCtx->frame_size
             << " pending=" << pendingFrame.samples << " stream=" << streamIndex;

    int frame_size = codecCtx->frame_size;
    int input_samples = frame->nb_samples;
    int bytes_per_sample = av_get_bytes_per_sample(codecCtx->sample_fmt);

    if (pendingFrame.next_pts == AV_NOPTS_VALUE) {
        pendingFrame.next_pts = (pts != AV_NOPTS_VALUE && pts >= 0) ? pts : 0;
    } else if (pendingFrame.samples == 0 && pts != AV_NOPTS_VALUE) {
        pendingFrame.next_pts = std::max(pendingFrame.next_pts, pts);
    }

    std::vector<uint8_t> merged_data[8];
    int total_samples = pendingFrame.samples + input_samples;

    for (int ch = 0; ch < codecCtx->ch_layout.nb_channels; ++ch) {
        merged_data[ch].resize(total_samples * bytes_per_sample);
        if (pendingFrame.samples > 0) {
            mempcpy(merged_data[ch].data(),
                    pendingFrame.data[ch].data(),
                    pendingFrame.samples * bytes_per_sample);
        }

        memcpy(merged_data[ch].data() + pendingFrame.samples * bytes_per_sample,
               frame->data[ch],
               input_samples * bytes_per_sample);
    }

    int total_full_frames = total_samples / frame_size;
    int remaining_samples = total_samples % frame_size;

    for (int i = 0; i < total_full_frames; i++) {
        AVFrame *sub_frame = av_frame_alloc();
        sub_frame->format = codecCtx->sample_fmt;
        sub_frame->ch_layout = codecCtx->ch_layout;
        sub_frame->ch_layout.nb_channels = codecCtx->ch_layout.nb_channels;
        sub_frame->sample_rate = codecCtx->sample_rate;
        sub_frame->nb_samples = frame_size;
        sub_frame->pts = pendingFrame.next_pts + i * frame_size;

        av_frame_get_buffer(sub_frame, 0);

        for (int ch = 0; ch < sub_frame->ch_layout.nb_channels; ch++) {
            uint8_t *src = merged_data[ch].data() + i * frame_size * bytes_per_sample;
            memcpy(sub_frame->data[ch], src, frame_size * bytes_per_sample);
        }

        {
            const char *sfmt = av_get_sample_fmt_name((AVSampleFormat) sub_frame->format);
            qCDebug(subrec::logging::frameLog) << "[AEnc] send_frame: nb_samples=" << sub_frame->nb_samples
                     << " pts=" << sub_frame->pts << " fmt=" << (sfmt ? sfmt : "unknown");
        }
        int ret_send = avcodec_send_frame(codecCtx, sub_frame);
        if (ret_send < 0) {
            printError(ret_send);
            av_frame_free(&sub_frame);
            return -1;
        }
        av_frame_free(&sub_frame);

        while (1) {
            AVPacket *pkt = av_packet_alloc();
            int ret = avcodec_receive_packet(codecCtx, pkt);

            if (ret == AVERROR(EAGAIN)) {
                av_packet_free(&pkt);
                break;
            } else if (ret < 0) {
                av_packet_free(&pkt);
                return -1;
            }

            pkt->stream_index = streamIndex;

            pkt->stream_index = streamIndex;
            pktQueue->enqueue(pkt);
        }
    }

    pendingFrame.next_pts = pendingFrame.next_pts + total_full_frames * frame_size;
    pendingFrame.samples = remaining_samples;
    for (int ch = 0; ch < codecCtx->ch_layout.nb_channels; ++ch) {
        pendingFrame.data[ch].resize(remaining_samples * bytes_per_sample);
        if (remaining_samples > 0) {
            uint8_t *src = merged_data[ch].data()
                           + total_full_frames * frame_size * bytes_per_sample;
            memcpy(pendingFrame.data[ch].data(), src, remaining_samples * bytes_per_sample);
        }
    }
    return 0;
}

void FFAEncoder::finish(int streamIndex)
{
    encode(nullptr, streamIndex, AV_NOPTS_VALUE, codecCtx ? codecCtx->time_base : AVRational{0, 1});
    if (pktQueue) {
        pktQueue->sendFinish();
    }
}

FFAEncoderPars *FFAEncoder::getEncoderPars()
{
    std::lock_guard<std::mutex> lock(mutex);
    return aPars;
}

AVCodecContext *FFAEncoder::getCodecCtx()
{
    return codecCtx;
}

void FFAEncoder::flush()
{
    std::lock_guard<std::mutex> lock(mutex);
    pendingFrame.samples = 0;
    pendingFrame.next_pts = AV_NOPTS_VALUE;
    for (int ch = 0; ch < 8; ++ch) {
        pendingFrame.data[ch].clear();
        pendingFrame.data[ch].shrink_to_fit();
    }
    qDebug() << "[AEnc] flush";
}

void FFAEncoder::resetPending(int64_t base_pts)
{
    std::lock_guard<std::mutex> lock(mutex);
    pendingFrame.next_pts = base_pts;
    pendingFrame.samples = 0;
    for (int ch = 0; ch < 8; ++ch) {
        pendingFrame.data[ch].clear();
        pendingFrame.data[ch].shrink_to_fit();
    }
    qDebug() << "[AEnc] resetPending base_pts=" << base_pts;
}

void FFAEncoder::initAudio(AVFrame *frame)
{
    std::lock_guard<std::mutex> lock(mutex);

    aPars = new FFAEncoderPars();
    aPars->biteRate = audio_encoding::kAacBitRate;
    aPars->nbChannel = frame->ch_layout.nb_channels;
    aPars->sampleRate = frame->sample_rate;
    aPars->audioFmt = AV_SAMPLE_FMT_FLTP;

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (codec == nullptr) {
        qDebug() << "Find AAC Codec Fail !";
        return;
    }

    codecCtx = avcodec_alloc_context3(codec);
    if (codecCtx == nullptr) {
        qDebug() << "Alloc CodecCtx Fail !";
        return;
    }

    codecCtx->bit_rate = aPars->biteRate;
    codecCtx->ch_layout.nb_channels = aPars->nbChannel;
    av_channel_layout_default(&codecCtx->ch_layout, aPars->nbChannel);
    codecCtx->sample_rate = aPars->sampleRate;
    codecCtx->sample_fmt = aPars->audioFmt;
    codecCtx->time_base = AVRational{1, aPars->sampleRate};
    codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    {
        const char *sfmt = av_get_sample_fmt_name(codecCtx->sample_fmt);
        qDebug() << "[AEnc] initAudio params: br=" << codecCtx->bit_rate
                 << " ch=" << codecCtx->ch_layout.nb_channels << " sr=" << codecCtx->sample_rate
                 << " sample_fmt=" << (sfmt ? sfmt : "unknown")
                 << " time_base=" << codecCtx->time_base.num << "/" << codecCtx->time_base.den;
    }

    int ret = avcodec_open2(codecCtx, codec, nullptr);
    if (ret < 0) {
        printError(ret);
        return;
    }

    // qDebug() << "[AEnc] opened: frame_size=" << codecCtx->frame_size
    //           << " profile=" << codecCtx->profile << " time_base=" << codecCtx->time_base.num << "/"
    //           << codecCtx->time_base.den << " sample_fmt="
    //           << (av_get_sample_fmt_name(codecCtx->sample_fmt)
    //                   ? av_get_sample_fmt_name(codecCtx->sample_fmt)
    //                   : "unknown")
    //           ;
}

void FFAEncoder::printError(int ret)
{
    char errorBuffer[AV_ERROR_MAX_STRING_SIZE];
    int res = av_strerror(ret, errorBuffer, sizeof errorBuffer);
    if (res < 0) {
        qDebug() << "Unknow Error!";
    } else {
        qDebug() << "Error:" << errorBuffer;
    }
}

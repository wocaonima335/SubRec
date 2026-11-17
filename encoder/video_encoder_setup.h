#ifndef VIDEO_ENCODER_SETUP_H
#define VIDEO_ENCODER_SETUP_H

#include "encoder/video_encoder_tuning.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
}

// Applies one encoder attempt's rate control to a freshly allocated context and returns the
// encoder's private options for avcodec_open2 (the caller frees them). Shared by
// FFVEncoder::initVideo and the ddagrab GPU-frame probe so a passing probe means the same
// configuration also opens for recording.
inline AVDictionary *applyEncoderAttempt(AVCodecContext *ctx,
                                         const video_tuning::EncoderAttempt &attempt,
                                         const video_tuning::RateLimits &rates)
{
    ctx->bit_rate = rates.target;
    if (attempt.legacy) {
        ctx->rc_max_rate = rates.target * 12 / 10;
        ctx->rc_buffer_size = static_cast<int>(rates.target);
        ctx->qmin = 10;
        ctx->qmax = 40;
        ctx->max_qdiff = 4;
    } else {
        // QP limits stay at the encoder defaults (full range).
        ctx->rc_max_rate = rates.peak;
        ctx->rc_buffer_size = static_cast<int>(rates.vbv);
    }
    if (attempt.globalQuality > 0)
        ctx->global_quality = attempt.globalQuality;

    AVDictionary *options = nullptr;
    for (const video_tuning::EncoderOption &option : attempt.options)
        av_dict_set(&options, option.key, option.value, 0);
    return options;
}

#endif // VIDEO_ENCODER_SETUP_H

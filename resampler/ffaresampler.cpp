#include "ffaresampler.h"
#include "decoder/ffadecoder.h"

FFAResampler::FFAResampler() {}

FFAResampler::~FFAResampler() {
  if (swrCtx) {
    swr_free(&swrCtx);
  }
  if (srcPars) {
    delete srcPars;
    srcPars = nullptr;
  }
  if (dstPars) {
    delete dstPars;
    dstPars = nullptr;
  }
}

void FFAResampler::init(FFAudioPars *src_, FFAudioPars *dst_) {
  srcPars = new FFAudioPars();
  memcpy(srcPars, src_, sizeof(FFAudioPars));

  dstPars = new FFAudioPars();
  memcpy(dstPars, dst_, sizeof(FFAudioPars));

  initSwr();
}

void FFAResampler::resample(AVFrame *srcFrame, AVFrame **dstFrame) {
  int64_t delaySamples = swr_get_delay(swrCtx, srcPars->sampleSize);
  int maxNbSamples =
      swr_get_out_samples(swrCtx, srcFrame->nb_samples + delaySamples);

  *dstFrame = allocFrame(dstPars, maxNbSamples, srcFrame);
  if (!*dstFrame) {
    qWarning() << "[AResampler] av_frame_alloc error!";
    return;
  }

  (*dstFrame)->pts = av_rescale_q(srcFrame->pts + delaySamples,
                                  (AVRational){1, srcPars->sampleRate},
                                  (AVRational){1, dstPars->sampleRate});

  int samples =
      swr_convert(swrCtx, (*dstFrame)->data, maxNbSamples,
                  (const uint8_t **)srcFrame->data, srcFrame->nb_samples);

  if (samples < 0) {
    printError(samples);
    swr_free(&swrCtx);
    return;
  }

  (*dstFrame)->nb_samples = samples;
}

void FFAResampler::initSwr() {
#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(57, 100, 0)
  // 旧版本使用av_get_default_channel_layout
  swrCtx = swr_alloc_set_opts(
      swrCtx, av_get_default_channel_layout(dstPars->nbChannels),
      dstPars->aFormatEnum, dstPars->sampleRate,
      av_get_default_channel_layout(srcPars->nbChannels), srcPars->aFormatEnum,
      srcPars->sampleRate, 0, nullptr);
  if (!swrCtx) {
    qWarning() << "[AResampler] initSwr error!";
    return;
  }

  int ret = swr_init(swrCtx);
  if (ret < 0) {
    printError(ret);
    swr_free(&swrCtx);
    return;
  }
#else
  // 初始化默认通道布局
  av_channel_layout_default(&srcLayout, srcPars->nbChannels);
  av_channel_layout_default(&dstLayout, dstPars->nbChannels);

  int ret = swr_alloc_set_opts2(
      &swrCtx, &dstLayout, dstPars->aFormatEnum, dstPars->sampleRate,
      &srcLayout, srcPars->aFormatEnum, srcPars->sampleRate, 0, nullptr);

  if (ret < 0) {
    qWarning() << "[AResampler] Swr Alloc set Opts Fail!";
    printError(ret);
    swrCtx = nullptr;
  }

  av_channel_layout_uninit(&srcLayout);
  av_channel_layout_uninit(&dstLayout);

  if (!swrCtx) {
    qWarning() << "[AResampler] initSwr error!";
    return;
  }

  ret = swr_init(swrCtx);
  if (ret < 0) {
    printError(ret);
    swr_free(&swrCtx);
    return;
  }
#endif
}

AVFrame *FFAResampler::allocFrame(FFAudioPars *aPars, int nbSamples,
                                  AVFrame *srcFrame) {
  AVFrame *frame = av_frame_alloc();
  if (!frame) {
    return nullptr;
  }

  frame->format = aPars->aFormatEnum;
  frame->sample_rate = aPars->sampleRate;
  frame->nb_samples = nbSamples;

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 100, 0)
  // FFmpeg 7.0+ 新API
  av_channel_layout_default(&frame->ch_layout, aPars->nbChannels);
#else
  // 旧版本兼容
  frame->channels = aPars->nbChannels;
  frame->channel_layout = av_get_default_channel_layout(aPars->nbChannels);
#endif

  if (srcFrame) {
    frame->pts = srcFrame->pts;
  }

  int ret = av_frame_get_buffer(frame, 0);
  if (ret < 0) {
    printError(ret);
    av_frame_unref(frame);
    av_frame_free(&frame);
    return nullptr;
  }

  return frame;
}

void FFAResampler::printError(int ret) {
  char errorBuffer[AV_ERROR_MAX_STRING_SIZE];
  int res = av_strerror(ret, errorBuffer, sizeof(errorBuffer));
  if (res < 0) {
    qWarning() << "[AResampler] Unknown Error!";
  } else {
    qWarning() << "[AResampler] Error:" << errorBuffer;
  }
}

#include "ffvresampler.h"
#include "decoder/ffvdecoder.h"

#include <QDebug>

FFVResampler::FFVResampler() {}

FFVResampler::~FFVResampler() {
  if (swsCtx) {
    sws_freeContext(swsCtx);
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

void FFVResampler::init(FFVideoPars *srcPars_, FFVideoPars *dstPars_) {
  srcPars = new FFVideoPars();
  memcpy(srcPars, srcPars_, sizeof(FFVideoPars));

  dstPars = new FFVideoPars();
  memcpy(dstPars, dstPars_, sizeof(FFVideoPars));

  qDebug() << "[VResampler] src:" << srcPars->width << "x" << srcPars->height
           << av_get_pix_fmt_name(srcPars->pixFmtEnum)
           << "-> dst:" << dstPars->width << "x" << dstPars->height
           << av_get_pix_fmt_name(dstPars->pixFmtEnum);

  initSws();
}

void FFVResampler::resample(AVFrame *srcFrame, AVFrame **dstFrame) {
  *dstFrame = allocFrame(dstPars, srcFrame);
  if (dstFrame == nullptr) {
    return;
  }

  sws_scale(swsCtx, srcFrame->data, srcFrame->linesize, 0, srcFrame->height,
            (*dstFrame)->data, (*dstFrame)->linesize);
}

AVFrame *FFVResampler::allocFrame(FFVideoPars *vPars, AVFrame *srcFrame) {
  AVFrame *frame = av_frame_alloc();
  if (!frame) {
    return nullptr;
  }

  frame->width = vPars->width;
  frame->height = vPars->height;
  frame->format = vPars->pixFmtEnum;
  frame->pts = srcFrame ? srcFrame->pts : AV_NOPTS_VALUE;

  int ret = av_frame_get_buffer(frame, 32);
  if (ret < 0) {
    printError(ret);
    av_frame_free(&frame);
    return nullptr;
  }

  ret = av_frame_make_writable(frame);
  if (ret < 0) {
    printError(ret);
    av_frame_free(&frame);
    return nullptr;
  }

  return frame;
}

void FFVResampler::initSws() {
  swsCtx = sws_getContext(srcPars->width, srcPars->height, srcPars->pixFmtEnum,
                          dstPars->width, dstPars->height, dstPars->pixFmtEnum,
                          SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
  if (!swsCtx) {
    qWarning() << "[VResampler] sws_getContext error!";
    return;
  }
}

void FFVResampler::printError(int ret) {
  char errorBuffer[AV_ERROR_MAX_STRING_SIZE];
  int res = av_strerror(ret, errorBuffer, sizeof errorBuffer);
  if (res < 0) {
    qWarning() << "[VResampler] Unknown Error!";
  } else {
    qWarning() << "[VResampler] Error:" << errorBuffer;
  }
}

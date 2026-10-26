#ifndef FFARESAMPLER_H
#define FFARESAMPLER_H

extern "C" {
#include "libavformat/avformat.h"
#include "libavutil/channel_layout.h"
#include "libswresample/swresample.h"
}
#include <QDebug>

class FFAudioPars;
class FFAResampler {
public:
  explicit FFAResampler();
  ~FFAResampler();

  void init(FFAudioPars *src, FFAudioPars *dst);
  void resample(AVFrame *srcFrame, AVFrame **dstFrame);

private:
  void initSwr();
  AVFrame *allocFrame(FFAudioPars *aPars, int nbSamples, AVFrame *srcFrame);
  void printError(int ret);

private:
  SwrContext *swrCtx = nullptr;
  FFAudioPars *srcPars = nullptr;
  FFAudioPars *dstPars = nullptr;

  AVChannelLayout srcLayout;
  AVChannelLayout dstLayout;
};

#endif // FFARESAMPLER_H

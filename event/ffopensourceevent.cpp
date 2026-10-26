#include "ffopensourceevent.h"

#include "thread/ffscreengrabthread.h"

extern "C" {
#include <libavutil/time.h>
}

using namespace FFRecordContextType;
#ifdef _WIN32
namespace {
class QueueAudioSink final : public capture::wasapi::AudioFrameSink {
    FFAFrameQueue *queue;
public:
    explicit QueueAudioSink(FFAFrameQueue *q) : queue(q) {}
    bool push(AVFrame *frame) override {
        queue->enqueue(frame);
        return !queue->isClosed();
    }
    void finish() override { queue->sendFinish(); }
};
}
#endif

FFOpenSourceEvent::FFOpenSourceEvent(FFRecorder *recorderCtx,
                                     demuxerType videoSourceType_,
                                     const string &videoUrl_,
                                     demuxerType audioSourceType_,
                                     const string &audioUrl_,
                                     const string &format_)
    : FFEvent(recorderCtx)
{
  videoSourceType = videoSourceType_;
  audioSourceType = audioSourceType_;
  videoUrl = videoUrl_;
  audioUrl = audioUrl_;
  videoFormat = audioFormat = format_;
  videoInput.kind = videoSourceType == SCREEN ? CaptureDeviceKind::Screen : CaptureDeviceKind::Camera;
  audioInput.kind = audioSourceType == AUDIO ? CaptureDeviceKind::SystemAudio : CaptureDeviceKind::Microphone;
  videoInput.url = videoInput.deviceId = videoInput.displayName = QString::fromStdString(videoUrl);
  audioInput.url = audioInput.deviceId = audioInput.displayName = QString::fromStdString(audioUrl);
  videoInput.format = audioInput.format = QString::fromStdString(format_);
  videoIndex = demuxerIndex[videoSourceType];
  audioIndex = demuxerIndex[audioSourceType];
}

FFOpenSourceEvent::FFOpenSourceEvent(FFRecorder *recorderCtx,
                                     const InputRequest &video,
                                     const InputRequest &audio)
    : FFOpenSourceEvent(recorderCtx,
                        video.kind == CaptureDeviceKind::Screen ? SCREEN : CAMERA,
                        video.url.toStdString(),
                        audio.kind == CaptureDeviceKind::SystemAudio ? AUDIO : MICROPHONE,
                        audio.url.toStdString(), video.format.toStdString())
{
  videoInput = video;
  audioInput = audio;
  audioFormat = audio.format.toStdString();
}

void FFOpenSourceEvent::work()
{
  openAndStart();
}

bool FFOpenSourceEvent::fail(const InputRequest &input, CaptureErrorStage stage,
                             int code, const QString &message)
{
  CaptureError error;
  error.stage = stage;
  error.role = input.kind;
  error.backend = input.backend;
  error.deviceId = input.deviceId;
  error.deviceName = input.displayName;
  error.ffmpegError = code;
  char text[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, text, sizeof(text));
  error.ffmpegMessage = QString::fromUtf8(text);
  error.userMessage = message;
  error.remediation = QStringLiteral("Refresh devices and verify the selected input and format.");
  startupResult = CaptureStartResult::failure(std::move(error));
  return false;
}

bool FFOpenSourceEvent::checkContinue(const InputRequest &input,
                                      const std::function<bool()> &shouldStart)
{
  if ((shouldStart && !shouldStart()) || !recoderContext || !recoderContext->isRecording())
    return fail(input, CaptureErrorStage::Configuration, AVERROR_EXIT,
                QStringLiteral("Capture start was cancelled."));
  return true;
}

bool FFOpenSourceEvent::openAndStart(const std::function<bool()> &shouldStart)
{
  return openAndStartResult(shouldStart).ok;
}

CaptureStartResult FFOpenSourceEvent::openAndStartResult(const std::function<bool()> &shouldStart)
{
  startupResult = CaptureStartResult::success();
  preparedScreen = false;
  usingScreenGrab = false;
  const auto unwind = [this] {
    if (preparedScreen && usingScreenGrab && screenGrabThread) {
      screenGrabThread->requestAbort();
      screenGrabThread->wait();
      screenGrabThread->close();
      preparedScreen = false;
    }
    if (preparedScreen) {
      // Joining can wait for a non-interruptible third-party driver open.
      vDemuxerThread[videoIndex]->requestAbort();
      vDemuxerThread[videoIndex]->wait();
      vDemuxerThread[videoIndex]->close();
      preparedScreen = false;
    }
    // Session owns abort/join/close of all other pipeline resources.
    return startupResult;
  };
  if (!checkContinue(audioInput, shouldStart)) return unwind();
  if (!initAudio(shouldStart)) return unwind();
  if (!checkContinue(videoInput, shouldStart)) return unwind();
  if (!initVideo(shouldStart)) return unwind();
  if (!checkContinue(videoInput, shouldStart)) return unwind();
  const int64_t t0 = av_gettime_relative();
  aEncoderThread->setStartTimeUs(t0);
  vEncoderThread->setStartTimeUs(t0);
  startAudio();
  if (!checkContinue(videoInput, shouldStart)) return unwind();
  startVideo();
  preparedScreen = false;
  if (!checkContinue(videoInput, shouldStart)) return startupResult;
  return CaptureStartResult::success();
}

bool FFOpenSourceEvent::initAudio(const std::function<bool()> &shouldStart)
{

  if (!aDemuxerThread[audioIndex] || !aDemuxer[audioIndex] ||
      !aDecoderThread[audioIndex] || !aDecoder[audioIndex] || !aFilterThread ||
      !aFilter || !aEncoderThread || !aEncoder || !muxer ||
      !aPktQueue[audioIndex] || !aFrmQueue[audioIndex] ||
      !aFilterEncoderFrmQueue || !aEncoderPktQueue)
  {
    return fail(audioInput, CaptureErrorStage::Configuration, AVERROR(EINVAL),
                QStringLiteral("Audio components not initialized."));
  }

  aFilterThread->stopEncoder();
  aFilterThread->stop();
  aFilterThread->wakeAllThread();
  aFilterThread->wait();
  aFilterThread->closeAudioSource(demuxerType::AUDIO);
  aFilterThread->closeAudioSource(demuxerType::MICROPHONE);

  aDemuxerThread[audioIndex]->requestAbort();
  aDemuxerThread[audioIndex]->wait();
  aDemuxerThread[audioIndex]->close();

  aDecoderThread[audioIndex]->requestAbort();
  aDecoderThread[audioIndex]->wait();
  aDecoderThread[audioIndex]->close();

  aEncoderThread->requestAbort();
  aEncoderThread->wait();
  aEncoderThread->close();

  if (!checkContinue(audioInput, shouldStart)) return false;
  usingWasapi = false;
#ifdef _WIN32
  auto *context = recoderContext->getContext();
  if (context->systemAudioCapture) {
    context->systemAudioCapture->requestAbort();
    aFrmQueue[A_AUDIO]->close();
    context->systemAudioCapture->join();
    context->systemAudioCapture.reset();
    context->systemAudioSink.reset();
  }
  if (audioSourceType == AUDIO && audioInput.backend == CaptureBackend::Wasapi) {
    context->systemAudioSink = std::make_unique<QueueAudioSink>(aFrmQueue[A_AUDIO]);
    context->systemAudioCapture = std::make_unique<capture::wasapi::LoopbackCapture>(capture::wasapi::LoopbackConfig{});
    auto *recorder = recoderContext;
    context->systemAudioCapture->onRuntimeError = [recorder](const capture::wasapi::LoopbackRuntimeError &error) {
      if (!error.recovered) QMetaObject::invokeMethod(recorder, "setCaptureError", Qt::QueuedConnection,
          Q_ARG(QString, error.operation + ": " + error.message),
          Q_ARG(QString, QStringLiteral("Check the playback device and restart recording.")));
    };
    startupResult = context->systemAudioCapture->open(context->systemAudioSink.get());
    usingWasapi = startupResult.ok;
    if (!usingWasapi) {
      context->systemAudioCapture->requestAbort();
      context->systemAudioCapture->join();
      if (!audioFallback || !checkContinue(audioInput, shouldStart)) return false;
      qWarning() << "[WASAPI] open failed; falling back to DirectShow";
      audioInput = *audioFallback;
      audioUrl = audioInput.url.toStdString();
      audioFormat = audioInput.format.toStdString();
    }
  }
#endif
  if (!usingWasapi) {
  aDemuxer[audioIndex]->init(audioUrl, audioFormat, aPktQueue[audioIndex], nullptr,
                             audioSourceType, true, shouldStart);
  aDemuxerThread[audioIndex]->init(aDemuxer[audioIndex]);

  if (!checkContinue(audioInput, shouldStart)) return false;
  startupResult = aDemuxer[audioIndex]->openResult(audioInput);
  if (!startupResult.ok) return false;
  if (!aDemuxer[audioIndex]->getAStream())
    return fail(audioInput, CaptureErrorStage::StreamInfo, AVERROR_STREAM_NOT_FOUND,
                QStringLiteral("The selected input has no audio stream."));
  aDecoder[audioIndex]->init(aDemuxer[audioIndex]->getAStream(),
                             aFrmQueue[audioIndex]);
  aDecoderThread[audioIndex]->init(aDecoder[audioIndex], aPktQueue[audioIndex]);
  }

  if (!checkContinue(audioInput, shouldStart)) return false;
  aEncoder->init(aEncoderPktQueue);
  aFilter->initalize(aFilterEncoderFrmQueue,
                     aDecoder[aDecoderType::A_AUDIO],
                     aDecoder[aDecoderType::A_MICROPHONE]);
  aFilterThread->init(aFrmQueue[aDecoderType::A_MICROPHONE],
                      aFrmQueue[aDecoderType::A_AUDIO],
                      aFilter);
  if (audioSourceType == demuxerType::AUDIO) {
    aFilterThread->openAudioSource(demuxerType::AUDIO);
    aFilterThread->closeAudioSource(demuxerType::MICROPHONE);
  } else {
    aFilterThread->closeAudioSource(demuxerType::AUDIO);
    aFilterThread->openAudioSource(demuxerType::MICROPHONE);
  }
  aEncoderThread->init(aFilter,
                       aEncoder,
                       muxer,
                       aFilterEncoderFrmQueue,
                       recoderContext->getSyncQueue());
  return true;
}

bool FFOpenSourceEvent::initVideo(const std::function<bool()> &shouldStart)
{

  if (!vDemuxerThread[videoIndex] || !vDemuxer[videoIndex] ||
      !vDecoderThread[videoIndex] || !vDecoder[videoIndex] || !vFilterThread ||
      !vFilter || !vEncoderThread || !vEncoder || !muxer || !muxerThread ||
      !vPktQueue[videoIndex] || !vFrmQueue[videoIndex] ||
      !vFilterEncoderFrmQueue || !vEncoderPktQueue)
  {
    return fail(videoInput, CaptureErrorStage::Configuration, AVERROR(EINVAL),
                QStringLiteral("Video components not initialized."));
  }

  vFilterThread->stopEncoder();
  vFilterThread->stop();
  vFilterThread->wakeAllThread();
  vFilterThread->wait();
  vFilterThread->closeVideoSource(demuxerType::SCREEN);
  vFilterThread->closeVideoSource(demuxerType::CAMERA);

  qDebug() << "[initVideo] Step 1: Stopping vDemuxerThread...";
  vDemuxerThread[videoIndex]->requestAbort();
  qDebug() << "[initVideo] Step 2: Abort requested on vDemuxerThread...";
  qDebug() << "[initVideo] Step 3: Waiting vDemuxerThread...";
  vDemuxerThread[videoIndex]->wait();
  qDebug() << "[initVideo] Step 4: Closing vDemuxerThread...";
  vDemuxerThread[videoIndex]->close();
  qDebug() << "[initVideo] Step 5: vPktQueue already closed by wake path";

  qDebug() << "[initVideo] Step 6: Stopping vDecoderThread...";
  vDecoderThread[videoIndex]->requestAbort();
  qDebug() << "[initVideo] Step 7: Abort requested on vDecoderThread...";
  qDebug() << "[initVideo] Step 8: Waiting vDecoderThread...";
  vDecoderThread[videoIndex]->wait();
  qDebug() << "[initVideo] Step 9: Closing vDecoderThread (will call "
              "vDecoder->close())...";
  vDecoderThread[videoIndex]->close();
  qDebug()
      << "[initVideo] Step 10: vDecoderThread closed";

  qDebug() << "[initVideo] Step 11: Stopping vEncoderThread...";
  vEncoderThread->requestAbort();
  qDebug() << "[initVideo] Step 12: Abort requested on vEncoderThread...";
  qDebug() << "[initVideo] Step 13: Waiting vEncoderThread...";
  vEncoderThread->wait();
  qDebug() << "[initVideo] Step 14: Closing vEncoderThread...";
  vEncoderThread->close();
  qDebug() << "[initVideo] Step 14.1: encoder input queue already closed by wake path";

  qDebug() << "[initVideo] Step 15: Stopping muxerThread...";
  muxerThread->stop();
  qDebug() << "[initVideo] Step 16: Waking muxerThread...";
  muxerThread->wakeAllThread();
  qDebug() << "[initVideo] Step 17: Waiting muxerThread...";
  muxerThread->wait();
  qDebug() << "[initVideo] Step 18: Closing muxerThread...";
  muxerThread->close();

  if (!checkContinue(videoInput, shouldStart)) return false;
  if (videoSourceType == demuxerType::SCREEN
      && videoInput.backend == CaptureBackend::DesktopDuplication) {
    qDebug() << "[initVideo] Step 19: Opening ddagrab screen source...";
    if (!openScreenGrab(shouldStart)) {
      if (!videoFallback || !checkContinue(videoInput, shouldStart)) return false;
      switchToVideoFallback();
    }
  }
  if (!usingScreenGrab) {
  qDebug() << "[initVideo] Step 19: Initializing vDemuxer...";
  const bool threadOwnedScreenOpen = (videoSourceType == demuxerType::SCREEN);
  vDemuxer[videoIndex]->init(videoUrl,
                             videoFormat,
                             nullptr,
                             vPktQueue[videoIndex],
                             videoSourceType,
                             !threadOwnedScreenOpen, shouldStart);
  qDebug() << "[initVideo] Step 20: Initializing vDemuxerThread...";
  vDemuxerThread[videoIndex]->init(vDemuxer[videoIndex]);

  if (threadOwnedScreenOpen) {
    qDebug() << "[initVideo] Step 20.1: Preparing screen demuxer on demux thread...";
    preparedScreen = true;
    if (!vDemuxerThread[videoIndex]->prepareOpenAndWait(5000, shouldStart)) {
      qDebug() << "[initVideo] Step 20.1 failed: screen demuxer prepare open failed";
      vDemuxerThread[videoIndex]->requestAbort();
      vDemuxerThread[videoIndex]->wait();
      if (!checkContinue(videoInput, shouldStart)) return false;
      startupResult = vDemuxer[videoIndex]->openResult(videoInput);
      if (startupResult.ok)
        return fail(videoInput, CaptureErrorStage::InputOpen, AVERROR(ETIMEDOUT),
                    QStringLiteral("Timed out waiting for the screen input to prepare."));
      return false;
    }
  }

  if (!checkContinue(videoInput, shouldStart)) return false;
  startupResult = vDemuxer[videoIndex]->openResult(videoInput);
  if (!startupResult.ok) return false;
  if (!vDemuxer[videoIndex]->getVStream())
    return fail(videoInput, CaptureErrorStage::StreamInfo, AVERROR_STREAM_NOT_FOUND,
                QStringLiteral("The selected input has no video stream."));
  qDebug() << "[initVideo] Step 21: Initializing vDecoder...";
  vDecoder[videoIndex]->init(vDemuxer[videoIndex]->getVStream(),
                             vFrmQueue[videoIndex]);
  qDebug() << "[initVideo] Step 22: Initializing vDecoderThread...";
  vDecoderThread[videoIndex]->init(vDecoder[videoIndex], vPktQueue[videoIndex]);
  } // !usingScreenGrab

  if (!checkContinue(videoInput, shouldStart)) return false;
  qDebug() << "[initVideo] Step 23: Initializing vEncoder...";
  vEncoder->init(vEncoderPktQueue);
  vFilterThread->init(vFrmQueue[videoIndex], vFilter);
  vFilter->init(vFilterEncoderFrmQueue, vDecoder[videoIndex]);
  vFilterThread->openVideoSource(videoSourceType);
  vFilterThread->closeVideoSource(videoSourceType == demuxerType::SCREEN
                                      ? demuxerType::CAMERA
                                      : demuxerType::SCREEN);
  qDebug() << "[initVideo] Step 24: Initializing vEncoderThread...";
  vEncoderThread->init(vFilter,
                       vEncoder,
                       muxer,
                       vFilterEncoderFrmQueue,
                       recoderContext->getSyncQueue(),
                       recoderContext);

  qDebug() << "[initVideo] Step 25: Getting video stream info...";
  AVStream *vStream = usingScreenGrab ? nullptr : vDemuxer[videoIndex]->getVStream();
  if (usingScreenGrab)
  {
    QString resolutionStr = QString("%1 X %2 : Screen")
                                .arg(screenGrabThread->width())
                                .arg(screenGrabThread->height());
    QMetaObject::invokeMethod(recoderContext, "setVideoResolution",
                              Qt::QueuedConnection,
                              Q_ARG(QString, resolutionStr));
  }
  else if (vStream && vStream->codecpar)
  {
    int width = vStream->codecpar->width;
    int height = vStream->codecpar->height;
    QString resolutionStr = QString("%1 X %2 : Screen").arg(width).arg(height);
    qDebug() << "screen size:" << width << height;
    QMetaObject::invokeMethod(recoderContext, "setVideoResolution",
                              Qt::QueuedConnection,
                              Q_ARG(QString, resolutionStr));
  }

  if (!checkContinue(videoInput, shouldStart)) return false;
  qDebug() << "[initVideo] Step 26: Initializing muxer...";
  QString fullPath =
      recoderContext->outputPath() + "/" + recoderContext->outputFileName();
  muxer->init(fullPath.toStdString());
  qDebug() << "[initVideo] Step 27: Initializing muxerThread...";
  muxerThread->init(aEncoderPktQueue, vEncoderPktQueue, muxer, aEncoder,
                    vEncoder, recoderContext);
  qDebug() << "[initVideo] Step 28: All done!";
  std::cout << "init opensource" << std::endl;
  return true;
}

void FFOpenSourceEvent::startAudio()
{
  if (!aDemuxerThread[audioIndex] || !aDecoderThread[audioIndex] ||
      !aEncoderThread || !recoderContext)
    return;

  auto *aEncPktQueue = recoderContext->getAEncoderPktQueue();
  if (!aEncPktQueue || !aPktQueue[audioIndex] || !aFrmQueue[audioIndex]
      || !aFilterEncoderFrmQueue || !aFilterThread)
    return;

  aEncPktQueue->start();

  aPktQueue[audioIndex]->start();
  aFrmQueue[audioIndex]->start();
  aFilterEncoderFrmQueue->start();

#ifdef _WIN32
  if (usingWasapi) recoderContext->getContext()->systemAudioCapture->start();
  else
#endif
  {
    aDemuxerThread[audioIndex]->start();
    aDecoderThread[audioIndex]->start();
  }

  aFilterThread->start();
  aFilterThread->startEncoder();
  aEncoderThread->start();
}

void FFOpenSourceEvent::startVideo()
{
  if (!vDemuxerThread[videoIndex] || !vDecoderThread[videoIndex] ||
      !vEncoderThread || !muxerThread || !recoderContext)
    return;

  auto *vEncPktQueue = recoderContext->getVEncoderPktQueue();
  if (!vEncPktQueue || !vPktQueue[videoIndex] || !vFrmQueue[videoIndex]
      || !vFilterEncoderFrmQueue || !vFilterThread)
    return;

  vEncPktQueue->start();
  vPktQueue[videoIndex]->start();
  vFrmQueue[videoIndex]->start();
  vFilterEncoderFrmQueue->start();

  if (usingScreenGrab) {
    // Frames go straight into vFrmQueue[SCREEN]; no demuxer/decoder thread runs.
    screenGrabThread->resume();
  } else {
    if (videoSourceType == demuxerType::SCREEN) {
      vDemuxerThread[videoIndex]->resumePreparedDemux();
    } else {
      vDemuxerThread[videoIndex]->start();
    }

    vDecoderThread[videoIndex]->start();
  }

  vFilterThread->start();
  vFilterThread->startEncoder();
  vEncoderThread->start();
  muxerThread->start();

  std::cout << "start opensource " << std::endl;
}

bool FFOpenSourceEvent::openScreenGrab(const std::function<bool()> &shouldStart)
{
  if (!screenGrabThread) {
    return fail(videoInput, CaptureErrorStage::Configuration, AVERROR(EINVAL),
                QStringLiteral("Screen grab thread not initialized."));
  }
  // A previous session may have left the thread finished but not closed.
  screenGrabThread->requestAbort();
  screenGrabThread->wait();
  screenGrabThread->close();
  vFrmQueue[videoIndex]->start();

  preparedScreen = true;
  usingScreenGrab = true; // unwind() aborts the grab thread while this is set
  startupResult = screenGrabThread->prepareOpenAndWait(
      videoInput, vFrmQueue[videoIndex], recoderContext, 10000, shouldStart);
  if (startupResult.ok) {
    qInfo().noquote() << "[initVideo] ddagrab screen ready:" << screenGrabThread->describe();
    return true;
  }
  qWarning().noquote() << "[initVideo] ddagrab screen failed:"
                       << (startupResult.error ? startupResult.error->userMessage : QString());
  screenGrabThread->requestAbort();
  screenGrabThread->wait();
  screenGrabThread->close();
  preparedScreen = false;
  usingScreenGrab = false;
  return false;
}

void FFOpenSourceEvent::switchToVideoFallback()
{
  qWarning().noquote() << "[initVideo] falling back to DirectShow screen:"
                       << videoFallback->displayName;
  videoInput = *videoFallback;
  videoUrl = videoInput.url.toStdString();
  videoFormat = videoInput.format.toStdString();
  // requestAbort() above closed the shared frame queue; the dshow decoder writes into it.
  vFrmQueue[videoIndex]->start();
  startupResult = CaptureStartResult::success();
}

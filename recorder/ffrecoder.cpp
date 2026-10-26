#include "ffrecorder.h"

#include "capture_session.h"
#include "capture_scheduler.h"

#include <QDebug>
#include <QStandardPaths>

#include <cstdarg>

extern "C" {
#include <libavutil/log.h>
}

namespace {
void ffmpegLogToQt(void *avcl, int level, const char *fmt, va_list vl) {
  if (level > av_log_get_level())
    return;
  char line[1024];
  int printPrefix = 1;
  av_log_format_line2(avcl, level, fmt, vl, line, sizeof(line), &printPrefix);
  const QString text = QString::fromUtf8(line).trimmed();
  if (!text.isEmpty())
    qDebug().noquote() << "[FFmpeg]" << text;
}
} // namespace

FFRecorder *FFRecorder::m_instance = nullptr;
std::mutex FFRecorder::m_mutex;

FFRecorder &FFRecorder::getInstance() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_instance) {
    m_instance = new FFRecorder();
  }
  return *m_instance;
}

void FFRecorder::destoryInstance() {
  std::lock_guard<std::mutex> lock(m_mutex);
  delete m_instance;
  m_instance = nullptr;
}

void FFRecorder::initialize() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_isRecording.load(std::memory_order_acquire) ||
      m_initialized.load(std::memory_order_acquire))
    return;
  // FFmpeg's own messages are off unless SUBREC_FFMPEG_LOG=info|verbose|debug (diagnostics
  // for ddagrab / encoder problems; the lines go to SubRec.log with an "[FFmpeg]" prefix).
  const QString ffmpegLog = qEnvironmentVariable("SUBREC_FFMPEG_LOG").trimmed().toLower();
  if (ffmpegLog.isEmpty()) {
    av_log_set_level(AV_LOG_QUIET);
  } else {
    av_log_set_level(ffmpegLog == QLatin1String("debug")     ? AV_LOG_DEBUG
                     : ffmpegLog == QLatin1String("verbose") ? AV_LOG_VERBOSE
                                                              : AV_LOG_INFO);
    av_log_set_callback(ffmpegLogToQt);
  }

  registerMetaTypes();
  initCoreComponents();
  m_initialized.store(true, std::memory_order_release);
  QMetaObject::invokeMethod(this, "audioSamplerChanged", Qt::QueuedConnection);
}

void FFRecorder::startRecord(const CaptureConfig &config) {
  if (!m_initialized.load(std::memory_order_acquire))
    initialize();

  qDebug() << "[FFRecorder] startRecord enter";
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_isRecording.load(std::memory_order_acquire))
    return;

  // 根据当前源重新绑定 video filter node，decoder 输出边指向当前视频源，
  // filter 输出边固定指向统一的 encoder 输入队列。
  const bool useScreen = config.video.kind == CaptureDeviceKind::Screen;
  demuxerType currentSource =
      useScreen ? demuxerType::SCREEN : demuxerType::CAMERA;
  d->vFilterThread->updateQueue(d->vDecoderFrmQueue[currentSource]);
  d->vFilter->updateDecoder(d->vDecoder[currentSource]);
  d->vFilterThread->openVideoSource(currentSource);
  d->vFilterThread->closeVideoSource(useScreen ? demuxerType::CAMERA
                                               : demuxerType::SCREEN);
  qDebug() << "[FFRecorder] video filter rebound";

  const bool useSystemAudio = config.audio.kind == CaptureDeviceKind::SystemAudio;
  if (useSystemAudio) {
    d->aFilterThread->openAudioSource(demuxerType::AUDIO);
    d->aFilterThread->closeAudioSource(demuxerType::MICROPHONE);
  } else {
    d->aFilterThread->closeAudioSource(demuxerType::AUDIO);
    d->aFilterThread->openAudioSource(demuxerType::MICROPHONE);
  }
  qDebug() << "[FFRecorder] audio source flags updated";
  d->aFilter->initalize(d->aFilterEncoderFrmQueue,
                        d->aDecoder[aDecoderType::A_AUDIO],
                        d->aDecoder[aDecoderType::A_MICROPHONE]);

  // 清空所有解码队列，防止残留数据导致时间戳混乱
  for (size_t i = 0; i < FFRecordContextType::V_DECODER_SIZE; i++) {
    if (d->vDecoderFrmQueue[i])
      d->vDecoderFrmQueue[i]->clearQueue();
    if (d->vDecoderPktQueue[i])
      d->vDecoderPktQueue[i]->clearQueue();
  }
  for (size_t i = 0; i < FFRecordContextType::A_DECODER_SIZE; i++) {
    if (d->aDecoderFrmQueue[i])
      d->aDecoderFrmQueue[i]->clearQueue();
    if (d->aDecoderPktQueue[i])
      d->aDecoderPktQueue[i]->clearQueue();
  }
  // 清空编码队列
  if (d->vEncoderPktQueue)
    d->vEncoderPktQueue->clearQueue();
  if (d->aEncoderPktQueue)
    d->aEncoderPktQueue->clearQueue();
  if (d->vFilterEncoderFrmQueue)
    d->vFilterEncoderFrmQueue->clearQueue();
  if (d->aFilterEncoderFrmQueue)
    d->aFilterEncoderFrmQueue->clearQueue();
  if (d->syncQueue)
    d->syncQueue->reset();
  qDebug() << "[FFRecorder] queues cleared";

  int64_t t0 = av_gettime_relative(); // 统一墙钟起点
  qDebug() << "[FFRecorder] provisional start clock prepared, real arm deferred until sources start:" << t0;
  d->audioSampler->clear();
  qDebug() << "[FFRecorder] audio sampler cleared";
  d->audioSampler->start();
  qDebug() << "[FFRecorder] audio sampler started";
  m_isRecording.store(true, std::memory_order_release);
  QMetaObject::invokeMethod(this, "isRecordingChanged", Qt::QueuedConnection);
  qDebug() << "[FFRecorder] startRecord leave";
}

void FFRecorder::applyPauseState(bool paused, int64_t tsUs) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (d->syncQueue) {
    d->syncQueue->onPauseChanged(paused, tsUs);
  }
  if (d->vFilterThread) {
    d->vFilterThread->onPauseChanged(paused, tsUs);
  }
  if (d->aFilterThread) {
    d->aFilterThread->onPauseChanged(paused, tsUs);
  }
  if (d->vEncoderThread) {
    d->vEncoderThread->onPauseChanged(paused, tsUs);
  }
  if (d->aEncoderThread) {
    d->aEncoderThread->onPauseChanged(paused, tsUs);
  }
}

void FFRecorder::stopRecord() {
  if (!m_isRecording.load(std::memory_order_acquire))
    return;

  qDebug() << "[FFRecorder] stopRecord enter";
  m_isRecording.store(false, std::memory_order_release);
  QMetaObject::invokeMethod(this, "isRecordingChanged", Qt::QueuedConnection);
  qDebug() << "[FFRecorder] recording flag cleared before shutdown";

  std::lock_guard<std::mutex> lock(m_mutex);
  d->audioSampler->stop();
  d->audioSampler->clear();
  qDebug() << "[FFRecorder] audio sampler stopped and cleared";
  qDebug() << "[FFRecorder] pipeline drain delegated to CaptureSession";

  setCaptureTimeText("00:00.0");
  qDebug() << "[FFRecorder] stopRecord leave";
}

FFRecorderPrivate *FFRecorder::getContext() { return d; }

void FFRecorder::initCoreComponents() {
  for (size_t i = 0; i < FFRecordContextType::A_DECODER_SIZE; i++) {
    d->aDecoderPktQueue[i] = new FFAPacketQueue;
    d->aDecoder[i] = new FFADecoder;
    d->aDecoderFrmQueue[i] = new FFAFrameQueue;
    d->aDecoderThread[i] = new FFADecoderThread;
    d->aDemuxer[i] = new Demuxer;
    d->aDemuxerThread[i] = new FFDemuxerThread;
  }

  for (size_t i = 0; i < FFRecordContextType::V_DECODER_SIZE; i++) {
    d->vDecoderPktQueue[i] = new FFVPacketQueue;
    d->vDecoder[i] = new FFVDecoder;
    d->vDecoderFrmQueue[i] = new FFVFrameQueue;
    d->vDecoderThread[i] = new FFVDecoderThread;
    d->vDemuxer[i] = new Demuxer;
    d->vDemuxerThread[i] = new FFDemuxerThread;
  }

  d->aEncoderPktQueue = new FFAPacketQueue();
  d->vEncoderPktQueue = new FFVPacketQueue();

  d->aFilterEncoderFrmQueue = new FFAFrameQueue();
  d->vFilterEncoderFrmQueue = new FFVFrameQueue();
  d->syncQueue = new RecorderSyncQueue();

  d->aFilter = new FFAFilter();
  d->vFilter = new FFVFilter();
  d->muxer = new FFMuxer();
  d->aEncoder = new FFAEncoder();
  d->vEncoder = new FFVEncoder();

  d->aFilterThread = new FFAFilterThread;
  d->vFilterThread = new FFVFilterThread;
  d->screenGrabThread = new FFScreenGrabThread;

  d->muxerThread = new FFMuxerThread;
  d->aEncoderThread = new FFAEncoderThread;
  d->vEncoderThread = new FFVEncoderThread;

  d->audioSampler = new FFAudioSampler(this);

  d->audioSampler->initialize(48000, 2, AV_SAMPLE_FMT_FLTP);

  d->aFilterThread->init(d->aDecoderFrmQueue[aDecoderType::A_MICROPHONE],
                         d->aDecoderFrmQueue[aDecoderType::A_AUDIO],
                         d->aFilter);
  d->aFilter->initalize(d->aFilterEncoderFrmQueue,
                        d->aDecoder[aDecoderType::A_AUDIO],
                        d->aDecoder[aDecoderType::A_MICROPHONE]);

  const bool useSystemAudio = true;
  if (useSystemAudio) {
    d->aFilterThread->openAudioSource(demuxerType::AUDIO);
    d->aFilterThread->closeAudioSource(demuxerType::MICROPHONE);
  } else {
    d->aFilterThread->closeAudioSource(demuxerType::AUDIO);
    d->aFilterThread->openAudioSource(demuxerType::MICROPHONE);
  }

  const bool useScreen = true;
  demuxerType initialSource =
      useScreen ? demuxerType::SCREEN : demuxerType::CAMERA;

  d->vFilterThread->init(d->vDecoderFrmQueue[initialSource], d->vFilter);
  d->vFilter->init(d->vFilterEncoderFrmQueue, d->vDecoder[initialSource]);
}

void FFRecorder::registerMetaTypes() {
  qRegisterMetaType<uint8_t *>("uint8_t*");
  qRegisterMetaType<AVFrame *>("AVFrame*");
  qRegisterMetaType<int64_t>("int64_t");
}

FFRecorder::FFRecorder(QObject *parent) {
  Q_UNUSED(parent);
  m_outputPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
  avdevice_register_all();
  avformat_network_init();
  d = new FFRecorderPrivate();
  m_scheduler = std::make_unique<CaptureScheduler>(*this);
}

FFRecorder::~FFRecorder() {
  m_scheduler.reset();
  stopRecord();
  delete d;
}

CaptureScheduler &FFRecorder::captureScheduler() { return *m_scheduler; }

const CaptureScheduler &FFRecorder::captureScheduler() const {
  return *m_scheduler;
}

FFVFilter *FFRecorder::getVFilter() { return d ? d->vFilter : nullptr; }

FFAFilter *FFRecorder::getAFilter() { return d ? d->aFilter : nullptr; }

FFAEncoder *FFRecorder::getAEncoder() { return d ? d->aEncoder : nullptr; }

FFVEncoder *FFRecorder::getVEncoder() { return d ? d->vEncoder : nullptr; }

FFMuxer *FFRecorder::getMuxer() { return d ? d->muxer : nullptr; }

FFAudioSampler *FFRecorder::getSampler() {
  return d ? d->audioSampler : nullptr;
}

QObject *FFRecorder::audioSampler() const {
  return d ? d->audioSampler : nullptr;
}

FFADecoder *FFRecorder::getADecoder(int index) {
  if (!d || index < 0 ||
      static_cast<size_t>(index) >= FFRecordContextType::A_DECODER_SIZE)
    return nullptr;
  return d->aDecoder[index];
}

FFVDecoder *FFRecorder::getVDecoder(int index) {
  if (!d || index < 0 ||
      static_cast<size_t>(index) >= FFRecordContextType::V_DECODER_SIZE)
    return nullptr;
  return d->vDecoder[index];
}

Demuxer *FFRecorder::getADemuxer(int index) {
  if (!d || index < 0 ||
      static_cast<size_t>(index) >= FFRecordContextType::A_DEMUXER_SIZE)
    return nullptr;
  return d->aDemuxer[index];
}

Demuxer *FFRecorder::getVDemuxer(int index) {
  if (!d || index < 0 ||
      static_cast<size_t>(index) >= FFRecordContextType::V_DEMUXER_SIZE)
    return nullptr;
  return d->vDemuxer[index];
}

FFDemuxerThread *FFRecorder::getADemuxerThread(int index) {
  if (!d || index < 0 ||
      static_cast<size_t>(index) >= FFRecordContextType::A_DEMUXER_SIZE)
    return nullptr;
  return d->aDemuxerThread[index];
}

FFDemuxerThread *FFRecorder::getVDemuxerThread(int index) {
  if (!d || index < 0 ||
      static_cast<size_t>(index) >= FFRecordContextType::V_DEMUXER_SIZE)
    return nullptr;
  return d->vDemuxerThread[index];
}

FFADecoderThread *FFRecorder::getADecoderThread(int index) {
  if (!d || index < 0 ||
      static_cast<size_t>(index) >= FFRecordContextType::A_DECODER_SIZE)
    return nullptr;
  return d->aDecoderThread[index];
}

FFVDecoderThread *FFRecorder::getVDecoderThread(int index) {
  if (!d || index < 0 ||
      static_cast<size_t>(index) >= FFRecordContextType::V_DECODER_SIZE)
    return nullptr;
  return d->vDecoderThread[index];
}

FFAEncoderThread *FFRecorder::getAEncoderThread() {
  return d ? d->aEncoderThread : nullptr;
}

FFVEncoderThread *FFRecorder::getVEncoderThread() {
  return d ? d->vEncoderThread : nullptr;
}

FFVFilterThread *FFRecorder::getVFilterThread() {
  return d ? d->vFilterThread : nullptr;
}

FFAFilterThread *FFRecorder::getAFilterThread() {
  return d ? d->aFilterThread : nullptr;
}

FFMuxerThread *FFRecorder::getMuxerThread() {
  return d ? d->muxerThread : nullptr;
}

FFScreenGrabThread *FFRecorder::getScreenGrabThread() {
  return d ? d->screenGrabThread : nullptr;
}

FFAPacketQueue *FFRecorder::getADecoderPktQueue(int index) {
  if (!d || index < 0 ||
      static_cast<size_t>(index) >= FFRecordContextType::A_DECODER_SIZE)
    return nullptr;
  return d->aDecoderPktQueue[index];
}

FFVPacketQueue *FFRecorder::getVDecoderPktQueue(int index) {
  if (!d || index < 0 ||
      static_cast<size_t>(index) >= FFRecordContextType::V_DECODER_SIZE)
    return nullptr;
  return d->vDecoderPktQueue[index];
}

FFVPacketQueue *FFRecorder::getVEncoderPktQueue() {
  return d ? d->vEncoderPktQueue : nullptr;
}

FFAPacketQueue *FFRecorder::getAEncoderPktQueue() {
  return d ? d->aEncoderPktQueue : nullptr;
}

FFAFrameQueue *FFRecorder::getADecoderFrmQueue(int index) {
  if (!d || index < 0 ||
      static_cast<size_t>(index) >= FFRecordContextType::A_DECODER_SIZE)
    return nullptr;
  return d->aDecoderFrmQueue[index];
}

FFVFrameQueue *FFRecorder::getVDecoderFrmQueue(int index) {
  if (!d || index < 0 ||
      static_cast<size_t>(index) >= FFRecordContextType::V_DECODER_SIZE)
    return nullptr;
  return d->vDecoderFrmQueue[index];
}

FFVFrameQueue *FFRecorder::getVFilterEncoderFrmQueue() {
  return d ? d->vFilterEncoderFrmQueue : nullptr;
}

FFAFrameQueue *FFRecorder::getAFilterEncoderFrmQueue() {
  return d ? d->aFilterEncoderFrmQueue : nullptr;
}

RecorderSyncQueue *FFRecorder::getSyncQueue() {
  return d ? d->syncQueue : nullptr;
}

QString FFRecorder::captureTimeText() const { return m_captureTimeText; }

void FFRecorder::setCaptureTimeText(const QString &timeText) {
  if (m_captureTimeText != timeText) {
    m_captureTimeText = timeText;
    emit captureTimeTextChanged();
  }
}

void FFRecorder::setOutputPath(const QString &path) {
  if (m_outputPath != path) {
    m_outputPath = path;
    emit outputPathChanged();
  }
}

void FFRecorder::setOutputFileName(const QString &fileName) {
  if (m_outputFileName != fileName) {
    m_outputFileName = fileName;
    emit outputFileNameChanged();
  }
}

void FFRecorder::setVideoResolution(const QString &resolution) {
  if (m_videoResolution != resolution) {
    m_videoResolution = resolution;
    emit videoResolutionChanged();
  }
}

bool FFRecorder::useSystemAudio() const {
  return m_scheduler && m_scheduler->useSystemAudio();
}

void FFRecorder::notifyAudioSourceChanged() { emit useSystemAudioChanged(); }

void FFRecorder::setSelectedVideoEncoder(const QString &name, bool isHardware,
                                         const QString &detail) {
  if (m_selectedVideoEncoder == name &&
      m_selectedVideoEncoderIsHardware == isHardware &&
      m_selectedVideoEncoderDetail == detail) {
    return;
  }

  m_selectedVideoEncoder = name;
  m_selectedVideoEncoderIsHardware = isHardware;
  m_selectedVideoEncoderDetail = detail;
  qInfo().noquote() << QStringLiteral("[Recorder] selectedVideoEncoder=%1 hardware=%2 detail=%3")
                            .arg(name)
                            .arg(isHardware ? QStringLiteral("true")
                                            : QStringLiteral("false"))
                            .arg(detail);
  emit selectedVideoEncoderChanged();
}

QVariantList FFRecorder::captureDevices() const {
  return m_scheduler ? m_scheduler->devices() : QVariantList{};
}

QString FFRecorder::selectedVideoDeviceId() const {
  return m_scheduler ? m_scheduler->selectedVideoDeviceId() : QString{};
}

QString FFRecorder::selectedAudioDeviceId() const {
  return m_scheduler ? m_scheduler->selectedAudioDeviceId() : QString{};
}

QString FFRecorder::captureState() const {
  if (!m_scheduler) return QStringLiteral("Idle");
  return captureSchedulerStateName(m_scheduler->state());
}

bool FFRecorder::canStartCapture() const {
  if (!m_scheduler) return false;
  const CaptureSchedulerState state = m_scheduler->state();
  return state == CaptureSchedulerState::Idle || state == CaptureSchedulerState::Failed;
}

void FFRecorder::notifyDeviceConfigurationChanged() { emit captureDevicesChanged(); }
void FFRecorder::notifyCaptureStateChanged() { emit captureStateChanged(); }

void FFRecorder::setCaptureError(const QString &message, const QString &remediation) {
  if (m_captureError == message && m_captureRemediation == remediation) return;
  m_captureError = message;
  m_captureRemediation = remediation;
  qWarning().noquote() << QStringLiteral("[CaptureError] message=%1 remediation=%2")
                                .arg(message, remediation);
  emit captureErrorChanged();
}

void FFRecorder::clearCaptureError() {
  if (m_captureError.isEmpty() && m_captureRemediation.isEmpty()) return;
  m_captureError.clear();
  m_captureRemediation.clear();
  emit captureErrorChanged();
}

void FFRecorder::refreshCaptureDevices() {
  if (m_scheduler) m_scheduler->refreshDevices();
}

bool FFRecorder::selectVideoCaptureDevice(const QString &deviceId) {
  return m_scheduler && m_scheduler->selectVideoDevice(deviceId);
}

bool FFRecorder::selectAudioCaptureDevice(const QString &deviceId) {
  return m_scheduler && m_scheduler->selectAudioDevice(deviceId);
}

QString FFRecorder::screenCaptureBackend() const {
  return m_scheduler ? screenBackendPreferenceName(m_scheduler->screenBackendPreference())
                     : QStringLiteral("auto");
}

bool FFRecorder::screenCaptureBackendFromEnvironment() const {
  return m_scheduler && m_scheduler->screenBackendFromEnvironment();
}

bool FFRecorder::setScreenCaptureBackend(const QString &name) {
  const auto preference = parseScreenBackendPreference(name);
  if (!preference || !m_scheduler) {
    qWarning() << "[FFRecorder] unknown screen capture backend" << name;
    return false;
  }
  return m_scheduler->setScreenBackendPreference(*preference);
}

void FFRecorder::retryCapture() {
  if (m_scheduler) m_scheduler->start();
}

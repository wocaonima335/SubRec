#ifndef FFRECORDER_H
#define FFRECORDER_H

#include "ffrecorder_p.h"

#include <QObject>
#include <QVariantList>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

using namespace FFRecordContextType;

class CaptureScheduler;
struct CaptureConfig;
class RecorderSyncQueue;

class FFRecorder : public QObject {
  Q_OBJECT;
  Q_PROPERTY(QString captureTimeText READ captureTimeText NOTIFY
                 captureTimeTextChanged)
  Q_PROPERTY(QObject *audioSampler READ audioSampler NOTIFY audioSamplerChanged)
  Q_PROPERTY(bool isRecording READ isRecording NOTIFY isRecordingChanged)
  Q_PROPERTY(QString outputPath READ outputPath WRITE setOutputPath NOTIFY
                 outputPathChanged)
  Q_PROPERTY(QString outputFileName READ outputFileName WRITE setOutputFileName
                 NOTIFY outputFileNameChanged)
  Q_PROPERTY(QString videoResolution READ videoResolution WRITE
                 setVideoResolution NOTIFY videoResolutionChanged)
  Q_PROPERTY(QString selectedVideoEncoder READ selectedVideoEncoder NOTIFY
                 selectedVideoEncoderChanged)
  Q_PROPERTY(bool selectedVideoEncoderIsHardware READ
                 selectedVideoEncoderIsHardware NOTIFY selectedVideoEncoderChanged)
  Q_PROPERTY(QString selectedVideoEncoderDetail READ selectedVideoEncoderDetail
                 NOTIFY selectedVideoEncoderChanged)
  Q_PROPERTY(bool useSystemAudio READ useSystemAudio NOTIFY useSystemAudioChanged)
  Q_PROPERTY(QVariantList captureDevices READ captureDevices NOTIFY captureDevicesChanged)
  Q_PROPERTY(QString selectedVideoDeviceId READ selectedVideoDeviceId NOTIFY captureDevicesChanged)
  Q_PROPERTY(QString selectedAudioDeviceId READ selectedAudioDeviceId NOTIFY captureDevicesChanged)
  Q_PROPERTY(QString captureState READ captureState NOTIFY captureStateChanged)
  Q_PROPERTY(QString captureError READ captureError NOTIFY captureErrorChanged)
  Q_PROPERTY(QString captureRemediation READ captureRemediation NOTIFY captureErrorChanged)
  Q_PROPERTY(bool canStartCapture READ canStartCapture NOTIFY captureStateChanged)
  // Screen backend "auto" | "ddagrab" | "dshow" (settings page; SUBREC_CAPTURE_BACKEND wins).
  Q_PROPERTY(QString screenCaptureBackend READ screenCaptureBackend NOTIFY captureDevicesChanged)
  Q_PROPERTY(bool screenCaptureBackendFromEnvironment READ screenCaptureBackendFromEnvironment
                 NOTIFY captureDevicesChanged)

public:
  static FFRecorder &getInstance();
  static void destoryInstance();

  FFRecorder(const FFRecorder &) = delete;
  FFRecorder &operator=(const FFRecorder &) = delete;
  void initialize();
  void startRecord(const CaptureConfig &config);
  void stopRecord();
  void applyPauseState(bool paused, int64_t tsUs);
  CaptureScheduler &captureScheduler();
  const CaptureScheduler &captureScheduler() const;

  class FFRecorderPrivate *getContext();

  FFADecoder *getADecoder(int index);
  FFVDecoder *getVDecoder(int index);
  Demuxer *getADemuxer(int index);
  Demuxer *getVDemuxer(int index);
  FFVFilter *getVFilter();
  FFAFilter *getAFilter();

  FFAEncoder *getAEncoder();
  FFVEncoder *getVEncoder();
  FFMuxer *getMuxer();

  FFAudioSampler *getSampler();

  FFDemuxerThread *getADemuxerThread(int index);
  FFDemuxerThread *getVDemuxerThread(int index);
  FFADecoderThread *getADecoderThread(int index);
  FFVDecoderThread *getVDecoderThread(int index);
  FFAEncoderThread *getAEncoderThread();
  FFVEncoderThread *getVEncoderThread();
  FFVFilterThread *getVFilterThread();
  FFAFilterThread *getAFilterThread();
  FFMuxerThread *getMuxerThread();
  FFScreenGrabThread *getScreenGrabThread();

  FFAPacketQueue *getADecoderPktQueue(int index);
  FFVPacketQueue *getVDecoderPktQueue(int index);

  FFVPacketQueue *getVEncoderPktQueue();
  FFAPacketQueue *getAEncoderPktQueue();

  FFAFrameQueue *getADecoderFrmQueue(int index);
  FFVFrameQueue *getVDecoderFrmQueue(int index);

  FFVFrameQueue *getVFilterEncoderFrmQueue();
  FFAFrameQueue *getAFilterEncoderFrmQueue();
  RecorderSyncQueue *getSyncQueue();

  QString captureTimeText() const;
  bool isRecording() const {
    return m_isRecording.load(std::memory_order_acquire);
  }
  QObject *audioSampler() const;

  QString outputPath() const { return m_outputPath; }
  void setOutputPath(const QString &path);

  QString outputFileName() const { return m_outputFileName; }
  void setOutputFileName(const QString &fileName);

  QString videoResolution() const { return m_videoResolution; }
  QString selectedVideoEncoder() const { return m_selectedVideoEncoder; }
  bool selectedVideoEncoderIsHardware() const {
    return m_selectedVideoEncoderIsHardware;
  }
  QString selectedVideoEncoderDetail() const {
    return m_selectedVideoEncoderDetail;
  }
  bool useSystemAudio() const;
  QVariantList captureDevices() const;
  QString selectedVideoDeviceId() const;
  QString selectedAudioDeviceId() const;
  QString captureState() const;
  QString captureError() const { return m_captureError; }
  QString captureRemediation() const { return m_captureRemediation; }
  bool canStartCapture() const;
  QString screenCaptureBackend() const;
  bool screenCaptureBackendFromEnvironment() const;

public slots:
  void setVideoResolution(const QString &resolution);
  void setCaptureTimeText(const QString &timeText);
  void setSelectedVideoEncoder(const QString &name, bool isHardware,
                               const QString &detail);
  void notifyAudioSourceChanged();
  void notifyDeviceConfigurationChanged();
  void notifyCaptureStateChanged();
  void setCaptureError(const QString &message, const QString &remediation);
  void clearCaptureError();
  Q_INVOKABLE void refreshCaptureDevices();
  Q_INVOKABLE bool selectVideoCaptureDevice(const QString &deviceId);
  Q_INVOKABLE bool selectAudioCaptureDevice(const QString &deviceId);
  Q_INVOKABLE bool setScreenCaptureBackend(const QString &name);
  Q_INVOKABLE void retryCapture();

signals:
  void captureTimeTextChanged();
  void audioSamplerChanged();
  void isRecordingChanged();
  void outputPathChanged();
  void outputFileNameChanged();
  void videoResolutionChanged();
  void selectedVideoEncoderChanged();
  void useSystemAudioChanged();
  void captureDevicesChanged();
  void captureStateChanged();
  void captureErrorChanged();

private:
  void initCoreComponents();
  void registerMetaTypes();

private:
  explicit FFRecorder(QObject *parent = nullptr);
  ~FFRecorder();

  FFRecorderPrivate *d = nullptr;
  static FFRecorder *m_instance;
  static std::mutex m_mutex;
  std::unique_ptr<CaptureScheduler> m_scheduler;

  std::atomic<bool> m_isRecording{false};
  std::atomic<bool> m_initialized{false};

  QString m_captureTimeText = "00:00.0";
  QString m_outputPath;
  QString m_outputFileName = "output.mp4";
  QString m_videoResolution = "0 X 0 : Screen";
  QString m_selectedVideoEncoder;
  bool m_selectedVideoEncoderIsHardware = false;
  QString m_selectedVideoEncoderDetail;
  QString m_captureError;
  QString m_captureRemediation;
};

#endif

#include "thread/failure_cleanup_deadline.h"
#include "capture/test_hook_gate.h"
#include "capture/autotest_result.h"
#include "capture/autotest_report.h"
#include "logging/frame_log.h"
#include "opengl/ffglitem.h"
#include "recorder/capture_scheduler.h"
#include "recorder/ffrecorder.h"
#include "autotest_shell_settings.h"
#include "shell/capture_exclusion.h"
#include "shell/power_request.h"
#include "shell/shell_settings.h"
#include "shell/shell_state_source.h"
#include "shell/single_instance.h"
#include "shell/tray_controller.h"
#include "shell/window_controller.h"


#include <QDebug>
#include <QDir>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QTemporaryDir>
#include <QUrl>
#include <QApplication>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QRect>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTimer>
#include <QVariant>
#include <QStringList>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <memory>

extern "C" {
#include <libavutil/time.h>
}

QFile logFile;

void qtLogHandler(QtMsgType type, const QMessageLogContext &,
                  const QString &msg)
{
  Q_UNUSED(type);
  static std::mutex m;
  std::lock_guard<std::mutex> lk(m);

  if (!logFile.isOpen())
  {
    // 使用绝对路径，确保日志文件可被找到
    QString logPath = qEnvironmentVariable("SUBREC_LOG_FILE").trimmed();
    if (logPath.isEmpty()) logPath = QCoreApplication::applicationDirPath() + "/SubRec.log";
    logFile.setFileName(logPath);

    if (!logFile.open(QIODevice::Append | QIODevice::Text))
    {
      // 如果打开失败，输出到标准错误
      fprintf(stderr, "Failed to open log file: %s\n", qPrintable(logPath));
      return;
    }
  }

  QTextStream ts(&logFile);
  ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ") << msg
     << Qt::endl;
}

void init() { FFRecorder::getInstance().captureScheduler().initialize(); }

// START INVARIANT (MINIMIZE_DESIGN.md §4.4 item 5, §8.4.5): every recording start —
// QML record button, Retry, tray, AutoTest — goes through
// WindowController::requestStart(), which turns capture exclusion on synchronously
// before the engine grabs its first frame. Only the engine* functions below touch the
// scheduler, and only RecordingActions (owned by WindowController) calls them.
static QPointer<subrec::shell::WindowController> g_shell;

static bool engineStart()
{
  qDebug() << "开始录制...";
  return FFRecorder::getInstance().captureScheduler().start();
}
static void engineStop()
{
  qDebug() << "停止录制...";
  FFRecorder::getInstance().captureScheduler().stop();
}
static void enginePause() { FFRecorder::getInstance().captureScheduler().pause(); }
static void engineResume() { FFRecorder::getInstance().captureScheduler().resume(); }

void startRecording()
{
  if (g_shell) g_shell->requestStart();
  else qWarning() << "[Shell] startRecording before WindowController exists; ignored";
}

// Stop does not touch exclusion (it is released on Idle/Failed by the controller).
// The controller's requestStop() only acts in Running/Paused; AutoTest scenarios may
// stop at fixed times in other states, so AutoTest keeps the direct engine stop to
// preserve its existing behavior. QML/tray stop goes through requestStop().
void stopRecording() { engineStop(); }

void pauseRecording()
{
  if (g_shell) g_shell->requestPause();
}

void readyRecording()
{
  if (g_shell) g_shell->requestResume();
}

// 点击屏幕/摄像头区域时触发的源打开控制事件
void openSourceByControl(bool useScreen)
{
  FFRecorder::getInstance().captureScheduler().changeVideoSource(useScreen);
}

void openAudioByControl(bool useSystemAudio)
{
  FFRecorder::getInstance().captureScheduler().changeAudioSource(useSystemAudio);
}

// Shared by duration completion and the finalized fatal-stop watcher.
static void logAutoTestRecordingResult(AutoTestExit result)
{
  auto &recorder = FFRecorder::getInstance();
  const QFileInfo file(recorder.outputPath() + "/" + recorder.outputFileName());
  qInfo().noquote() << autoTestRecordingReport(
      result, file.absoluteFilePath(), file.exists() ? file.size() : -1,
      recorder.selectedVideoEncoder(), recorder.selectedVideoEncoderIsHardware(),
      recorder.videoResolution(), recorder.selectedVideoEncoderDetail(), recorder.captureError());
}

// SUBREC_AUTOTEST_OUTPUT=<dir>/<name>.mp4: output folder and file name for this run.
// SUBREC_AUTOTEST_MIC_DEVICE=<name or id>: switch the audio source to the microphone and select
// the DirectShow microphone whose id or display name equals it (case-insensitive), or else the
// only one that contains it. Returns false (and logs the candidates) when nothing matches.
bool applyAutoTestEnvironment()
{
  auto &recorder = FFRecorder::getInstance();
  auto &scheduler = recorder.captureScheduler();

  const QString output = qEnvironmentVariable("SUBREC_AUTOTEST_OUTPUT").trimmed();
  if (!output.isEmpty()) {
    const QFileInfo info(QDir::fromNativeSeparators(output));
    if (info.fileName().isEmpty() || !QDir().mkpath(info.absolutePath())) {
      qWarning() << "[AutoTest] SUBREC_AUTOTEST_OUTPUT unusable:" << output;
      return false;
    }
    recorder.setOutputPath(info.absolutePath());
    recorder.setOutputFileName(info.fileName());
    qInfo() << "[AutoTest] output =" << info.absoluteFilePath();
  }

  const QString requestedMode = qEnvironmentVariable("SUBREC_AUTOTEST_AUDIO_MODE").trimmed().toLower();
  const bool hasExplicitMode = !requestedMode.isEmpty();
  const bool withSystem = qEnvironmentVariableIntValue("SUBREC_AUTOTEST_SYSTEM_AUDIO") == 1;
  const QString wanted = qEnvironmentVariable("SUBREC_AUTOTEST_MIC_DEVICE").trimmed();
  QString chosen;
  if (!wanted.isEmpty()) {
    QStringList candidates;
    QString exact;
    QStringList partial;
    for (const QVariant &value : scheduler.devices()) {
      const QVariantMap device = value.toMap();
      if (device.value(QStringLiteral("kind")).toString() != QLatin1String("microphone"))
        continue;
      const QString id = device.value(QStringLiteral("id")).toString();
      const QString name = device.value(QStringLiteral("displayName")).toString();
      candidates << QStringLiteral("'%1' (id '%2', available=%3)")
                        .arg(name, id, device.value(QStringLiteral("available")).toBool()
                                           ? QStringLiteral("yes") : QStringLiteral("no"));
      if (id.compare(wanted, Qt::CaseInsensitive) == 0
          || name.compare(wanted, Qt::CaseInsensitive) == 0)
        exact = id;
      else if (name.contains(wanted, Qt::CaseInsensitive) || id.contains(wanted, Qt::CaseInsensitive))
        partial << id;
    }
    chosen = !exact.isEmpty() ? exact : (partial.size() == 1 ? partial.first() : QString());
    if (chosen.isEmpty()) {
      qWarning().noquote() << "[AutoTest] SUBREC_AUTOTEST_MIC_DEVICE=" + wanted
                                  + " matches no single available microphone; candidates: "
                                  + (candidates.isEmpty() ? QStringLiteral("none") : candidates.join(QStringLiteral(", ")));
      return false;
    }
  }

  if (hasExplicitMode) {
    if (requestedMode != QLatin1String("system") && requestedMode != QLatin1String("microphone")
        && requestedMode != QLatin1String("both") && requestedMode != QLatin1String("none")) {
      qWarning() << "[AutoTest] invalid SUBREC_AUTOTEST_AUDIO_MODE =" << requestedMode;
      qWarning() << "[AutoTest] audio mode could not be applied:" << scheduler.lastError();
      return false;
    }
    bool applied = false;
    if (requestedMode == QLatin1String("both")) {
      applied = scheduler.setAudioMode(QStringLiteral("system"));
      if (applied) applied = chosen.isEmpty() ? scheduler.setAudioMode(QStringLiteral("both"))
                                             : scheduler.selectMixMicrophone(chosen);
    } else {
      if (!chosen.isEmpty())
        applied = scheduler.selectMicrophoneDevice(chosen);
      else
        applied = true;
      if (applied) applied = scheduler.setAudioMode(requestedMode);
    }
    const QString actualMic = scheduler.mixMicrophoneId().isEmpty()
                                  ? scheduler.selectedMicrophoneId() : scheduler.mixMicrophoneId();
    qInfo().noquote() << QStringLiteral("[AutoTest] audio mode = %1 mic=%2")
                             .arg(requestedMode, actualMic.isEmpty() ? QStringLiteral("-") : actualMic);
    if (!applied) {
      qWarning() << "[AutoTest] audio mode could not be applied:" << scheduler.lastError();
      return false;
    }
  } else if (withSystem) {
    if (!scheduler.changeAudioSource(true)
        || (!chosen.isEmpty() && !scheduler.selectMixMicrophone(chosen))) {
      qWarning() << "[AutoTest] microphone mix could not be selected:" << scheduler.lastError();
      return false;
    }
  } else if (!wanted.isEmpty()) {
    if (!scheduler.changeAudioSource(false) || !scheduler.selectAudioDevice(chosen)) {
      qWarning() << "[AutoTest] microphone could not be selected:" << scheduler.lastError();
      return false;
    }
  }
  return true;
}

void runAutoScenario(QObject *root)
{
  if (!capture::test::autoTestScenarioActive())
    return;
  const QString scenario = qEnvironmentVariable("SUBREC_AUTOTEST_SCENARIO").trimmed();

  if (scenario == QStringLiteral("ui_probe")) {
#if !defined(SUBREC_STRIP_TEST_HOOKS) || !SUBREC_STRIP_TEST_HOOKS
    QTimer::singleShot(250, root, [root] {
      auto &recorder = FFRecorder::getInstance();
      auto *banner = root->findChild<QObject *>(QStringLiteral("captureErrorBanner"));
      auto *close = root->findChild<QObject *>(QStringLiteral("dismissCaptureError"));
      recorder.setCaptureError(QStringLiteral("ui_probe close"), QStringLiteral("Dismiss this error"));
      const bool shown = banner && banner->property("visible").toBool();
      const bool clicked = close && QMetaObject::invokeMethod(close, "click");
      const bool closeOk = shown && clicked && recorder.captureError().isEmpty()
                           && !banner->property("visible").toBool();
      recorder.setCaptureError(QStringLiteral("ui_probe refresh"), QString());
      recorder.refreshCaptureDevices();
      const bool autoOk = recorder.captureError().isEmpty()
                          && banner && !banner->property("visible").toBool();
      if (auto *rootItem = qobject_cast<QObject *>(root)) {
        QMetaObject::invokeMethod(rootItem, "handleSettingItemClick", Qt::DirectConnection,
                                  Q_ARG(QVariant, QVariant(1)), Q_ARG(QVariant, QVariant(QStringLiteral("settings"))));
      }
      QTimer::singleShot(500, root, [root, closeOk, autoOk] {
        auto *dialog = root->findChild<QObject *>(QStringLiteral("folderDialog"));
        bool opened = dialog && QMetaObject::invokeMethod(dialog, "open");
        QTimer::singleShot(250, root, [root, dialog, closeOk, autoOk, opened] {
          const bool visible = opened && dialog && dialog->property("visible").toBool();
          if (dialog) QMetaObject::invokeMethod(dialog, "close");
          qInfo().noquote() << QStringLiteral("[AutoTest] ui_probe M1_close=%1 M1_auto=%2 I3_dialog=%3")
              .arg(closeOk ? "PASS" : "FAIL", autoOk ? "PASS" : "FAIL",
                   visible ? "PASS" : "FAIL");
          QCoreApplication::exit(closeOk && autoOk && visible ? AutoTestOk : AutoTestCaptureError);
        });
      });
    });
#else
    QTimer::singleShot(0, [] { QCoreApplication::exit(AutoTestBadEnvironment); });
#endif
    return;
  }

  const int quitDelay = qEnvironmentVariableIntValue(
      "SUBREC_AUTOTEST_QUIT_MS");
  int finalQuitDelay = quitDelay > 0 ? quitDelay : 5000;

  qDebug() << "[AutoTest] scenario =" << scenario;
  qInfo() << "[AutoTest] SUBREC_CAPTURE_BACKEND ="
          << qEnvironmentVariable("SUBREC_CAPTURE_BACKEND")
          << "screen backend =" << FFRecorder::getInstance().screenCaptureBackend();
  if (!applyAutoTestEnvironment()) {
    QTimer::singleShot(0, [] { QCoreApplication::exit(AutoTestBadEnvironment); });
    return;
  }

  auto schedule = [](int delayMs, auto callback) {
    QTimer::singleShot(delayMs, callback);
  };

  auto autoStateName = [](CaptureSchedulerState state) {
    switch (state) {
    case CaptureSchedulerState::Idle:
      return QStringLiteral("Idle");
    case CaptureSchedulerState::Preparing:
      return QStringLiteral("Preparing");
    case CaptureSchedulerState::Running:
      return QStringLiteral("Running");
    case CaptureSchedulerState::Paused:
      return QStringLiteral("Paused");
    case CaptureSchedulerState::Stopping:
      return QStringLiteral("Stopping");
    case CaptureSchedulerState::Failed:
      return QStringLiteral("Failed");
    }

    return QStringLiteral("Unknown");
  };

  auto quitIssued = std::make_shared<std::atomic_bool>(false);
  auto requestQuit = [quitIssued]() {
    if (quitIssued->exchange(true, std::memory_order_acq_rel)) {
      return;
    }

    qDebug() << "[AutoTest] quit";
    QCoreApplication::quit();

    QTimer::singleShot(1500, [] {
      qWarning() << "[AutoTest] quit timeout, force exit";
      ::_Exit(0);
    });
  };

  // A fatal worker error can stop a recording before its configured duration.
  // Keep watching independently of the duration timers and report it as soon
  // as cleanup reaches Idle/Failed (the output trailer has then been handled).
  if (scenario == QLatin1String("record_seconds") ||
      scenario == QLatin1String("pause_cycles") ||
      scenario == QLatin1String("mode_cycle")) {
    auto sawRunning = std::make_shared<bool>(false);
    auto watchFatal = std::make_shared<std::function<void()>>();
    *watchFatal = [watchFatal, sawRunning, quitIssued]() {
      auto &recorder = FFRecorder::getInstance();
      const auto state = recorder.captureScheduler().state();
      if (state == CaptureSchedulerState::Running || state == CaptureSchedulerState::Paused)
        *sawRunning = true;
      const bool finalized = state == CaptureSchedulerState::Idle ||
                             state == CaptureSchedulerState::Failed;
      if (*sawRunning && autoTestCaptureFailureReady(finalized,
                                                     recorder.captureError().isEmpty())) {
        if (!quitIssued->exchange(true, std::memory_order_acq_rel)) {
          logAutoTestRecordingResult(AutoTestCaptureError);
          qInfo() << "[AutoTest] exit code" << AutoTestCaptureError;
          const auto grace = capture_stop::timeout() + std::chrono::milliseconds(1500);
          capture_stop::armAutoTestExitWatchdog(AutoTestCaptureError, grace);
          QCoreApplication::exit(AutoTestCaptureError);
        }
        return;
      }
      QTimer::singleShot(100, [watchFatal] { (*watchFatal)(); });
    };
    QTimer::singleShot(0, [watchFatal] { (*watchFatal)(); });
  }

  auto scheduleWhenState =
      [autoStateName](CaptureSchedulerState targetState,
                      int timeoutMs,
                      std::function<void()> callback,
                      const QString &tag) {
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        auto poller = std::make_shared<std::function<void()>>();
        *poller = [targetState, timeoutMs, callback = std::move(callback), tag,
                   timer, poller, autoStateName]() mutable {
          const CaptureSchedulerState currentState =
              FFRecorder::getInstance().captureScheduler().state();
          if (currentState == targetState) {
            qDebug() << "[AutoTest] state reached" << tag
                     << autoStateName(currentState);
            callback();
            return;
          }

          if (timer->elapsed() >= timeoutMs) {
            qWarning() << "[AutoTest] wait state timeout" << tag
                       << "target=" << autoStateName(targetState)
                       << "current=" << autoStateName(currentState)
                       << "timeoutMs=" << timeoutMs;
            return;
          }

          QTimer::singleShot(100, [poller] { (*poller)(); });
        };

        QTimer::singleShot(100, [poller] { (*poller)(); });
      };

  if (scenario == "start_then_open_camera") {
    schedule(200, [] { openSourceByControl(false); });
    schedule(500, [] { startRecording(); });
    schedule(2200, [] { stopRecording(); });
  } else if (scenario == "start_then_open_camera_only") {
    schedule(200, [] { openSourceByControl(false); });
    schedule(500, [] { startRecording(); });
  } else if (scenario == "start_then_open_screen") {
    schedule(200, [] { startRecording(); });
    schedule(500, [] { openSourceByControl(true); });
    schedule(2000, [] { stopRecording(); });
  } else if (scenario == "start_then_open_screen_only") {
    schedule(200, [] { startRecording(); });
    schedule(500, [] { openSourceByControl(true); });
  } else if (scenario == "start_then_stop") {
    schedule(200, [] { startRecording(); });
    schedule(1200, [] { stopRecording(); });
  } else if (scenario == "select_system_then_start_stop") {
    schedule(100, [root] { QMetaObject::invokeMethod(root, "openSystemAudio"); });
    schedule(300, [] { startRecording(); });
    schedule(1800, [] { stopRecording(); });
  } else if (scenario == "select_microphone_then_start_stop") {
    schedule(100, [root] { QMetaObject::invokeMethod(root, "openMicrophone"); });
    schedule(300, [] { startRecording(); });
    schedule(1800, [] { stopRecording(); });
  } else if (scenario == "start_wait5_stop") {
    schedule(200, [] { startRecording(); });
    scheduleWhenState(CaptureSchedulerState::Running,
                      15000,
                      [schedule, scheduleWhenState, requestQuit] {
                        schedule(5000, [scheduleWhenState, requestQuit] {
                          stopRecording();
                          scheduleWhenState(CaptureSchedulerState::Idle,
                                            15000,
                                            requestQuit,
                                            QStringLiteral("idle-after-stop"));
                        });
                      },
                      QStringLiteral("running-before-stop"));
  } else if (scenario == "start_stop_start_stop") {
    schedule(200, [] { startRecording(); });
    scheduleWhenState(
        CaptureSchedulerState::Running,
        15000,
        [schedule, scheduleWhenState, requestQuit] {
          schedule(3000, [schedule, scheduleWhenState, requestQuit] {
            stopRecording();
            scheduleWhenState(
                CaptureSchedulerState::Idle,
                15000,
                [schedule, scheduleWhenState, requestQuit] {
                  startRecording();
                  scheduleWhenState(
                      CaptureSchedulerState::Running,
                      15000,
                      [schedule, scheduleWhenState, requestQuit] {
                        schedule(5000, [scheduleWhenState, requestQuit] {
                          stopRecording();
                          scheduleWhenState(CaptureSchedulerState::Idle,
                                            15000,
                                            requestQuit,
                                            QStringLiteral("idle-after-second-stop"));
                        });
                      },
                      QStringLiteral("second-running-before-stop"));
                },
                QStringLiteral("idle-before-second-start"));
          });
        },
        QStringLiteral("first-running-before-stop"));
  } else if (scenario == "start_then_pause_resume_stop") {
    schedule(200, [] { startRecording(); });
    scheduleWhenState(
        CaptureSchedulerState::Running,
        15000,
        [schedule, scheduleWhenState, requestQuit] {
          pauseRecording();
          scheduleWhenState(
              CaptureSchedulerState::Paused,
              5000,
              [schedule, scheduleWhenState, requestQuit] {
                readyRecording();
                scheduleWhenState(
                    CaptureSchedulerState::Running,
                    5000,
                    [schedule, scheduleWhenState, requestQuit] {
                      schedule(3000, [scheduleWhenState, requestQuit] {
                        stopRecording();
                        scheduleWhenState(CaptureSchedulerState::Idle,
                                          15000,
                                          requestQuit,
                                          QStringLiteral("idle-after-pause-resume-stop"));
                      });
                    },
                    QStringLiteral("running-after-resume"));
              },
              QStringLiteral("paused-before-resume"));
        },
        QStringLiteral("running-before-pause"));
  } else if (scenario == "record_then_settings_page") {
    schedule(200, [] { startRecording(); });
    schedule(500, [root] {
      QMetaObject::invokeMethod(root, "handleSettingItemClick",
                                Q_ARG(int, 1),
                                Q_ARG(QString, QStringLiteral("settings")));
    });
    schedule(2000, [] { stopRecording(); });
  } else if (scenario == "record_then_settings_page_only") {
    schedule(200, [] { startRecording(); });
    schedule(500, [root] {
      QMetaObject::invokeMethod(root, "handleSettingItemClick",
                                Q_ARG(int, 1),
                                Q_ARG(QString, QStringLiteral("settings")));
    });
  } else if (scenario == "record_then_video_list_page") {
    schedule(200, [] { startRecording(); });
    schedule(500, [root] {
      QMetaObject::invokeMethod(root, "handleSettingItemClick",
                                Q_ARG(int, 2),
                                Q_ARG(QString, QStringLiteral("video-list")));
    });
    schedule(2000, [] { stopRecording(); });
  } else if (scenario == "record_then_video_list_page_only") {
    schedule(200, [] { startRecording(); });
    schedule(500, [root] {
      QMetaObject::invokeMethod(root, "handleSettingItemClick",
                                Q_ARG(int, 2),
                                Q_ARG(QString, QStringLiteral("video-list")));
    });
  } else if (scenario == "phase4_long_record_dynamic_screen") {
    schedule(100, [] { openSourceByControl(true); });
    schedule(300, [] { startRecording(); });
    scheduleWhenState(
        CaptureSchedulerState::Running,
        15000,
        [root, schedule, scheduleWhenState, requestQuit, finalQuitDelay] {
          auto window = qobject_cast<QQuickWindow *>(root);
          QTimer *moveTimer = nullptr;
          if (window) {
            const auto positions = std::make_shared<QVector<QRect>>(QVector<QRect>{
                QRect(40, 40, 640, 500),
                QRect(220, 60, 700, 520),
                QRect(120, 140, 760, 540),
                QRect(280, 110, 680, 510)});
            const auto stepIndex = std::make_shared<int>(0);
            moveTimer = new QTimer(root);
            moveTimer->setInterval(700);
            QObject::connect(moveTimer, &QTimer::timeout, root,
                             [window, positions, stepIndex] {
                               if (positions->isEmpty()) {
                                 return;
                               }
                               const QRect rect =
                                   positions->at(*stepIndex % positions->size());
                               ++(*stepIndex);
                               window->setX(rect.x());
                               window->setY(rect.y());
                               window->setWidth(rect.width());
                               window->setHeight(rect.height());
                               qDebug() << "[AutoTest] dynamic scene step"
                                        << *stepIndex << rect;
                             });
            moveTimer->start();
          } else {
            qWarning() << "[AutoTest] dynamic scene: root is not a QQuickWindow";
          }

          const int stopDelayMs = std::max(1500, finalQuitDelay - 2000);
          schedule(stopDelayMs,
                   [moveTimer, scheduleWhenState, requestQuit] {
                     if (moveTimer) {
                       moveTimer->stop();
                     }
                     stopRecording();
                     scheduleWhenState(
                         CaptureSchedulerState::Idle,
                         15000,
                         [] {
                           auto &recorder = FFRecorder::getInstance();
                           const QFileInfo file(recorder.outputPath() + "/" + recorder.outputFileName());
                           const auto result = autoTestRecordingResult(file.exists(), file.size(), recorder.captureError().isEmpty());
                           qInfo().noquote() << QStringLiteral("[AutoTest] result=%1 captureError=%2")
                               .arg(result == AutoTestOk ? QStringLiteral("PASS") : QStringLiteral("FAIL"), recorder.captureError());
                           qInfo() << "[AutoTest] quit exit code" << result;
                           QCoreApplication::exit(result);
                         },
                         QStringLiteral("idle-after-phase4-long-dynamic-stop"));
                   });
        },
        QStringLiteral("running-before-phase4-long-dynamic"));
  } else if (scenario == "mode_cycle") {
    const int recordMsEnv = qEnvironmentVariableIntValue("SUBREC_AUTOTEST_RECORD_MS");
    const int recordMs = recordMsEnv > 0 ? recordMsEnv : 5000;
    const QString sequenceText = qEnvironmentVariable("SUBREC_AUTOTEST_MODE_SEQUENCE").trimmed();
    QStringList modes = (sequenceText.isEmpty()
                             ? QStringLiteral("system,microphone,both,none,system")
                             : sequenceText).split(',', Qt::SkipEmptyParts);
    for (QString &mode : modes) mode = mode.trimmed().toLower();
    modes.removeAll(QString());
    if (modes.isEmpty()) {
      qWarning() << "[AutoTest] mode cycle has no modes";
      QTimer::singleShot(0, [quitIssued] {
        if (!quitIssued->exchange(true, std::memory_order_acq_rel))
          QCoreApplication::exit(AutoTestBadEnvironment);
      });
      return;
    }
    if (quitDelay <= 0)
      finalQuitDelay = modes.size() * (recordMs + 50000) + 10000;
    auto exitWith = [quitIssued](int code) {
      if (quitIssued->exchange(true, std::memory_order_acq_rel)) return;
      qInfo() << "[AutoTest] exit code" << code;
      const auto grace = capture_stop::timeout() + std::chrono::milliseconds(1500);
      capture_stop::armAutoTestExitWatchdog(code, grace);
      QCoreApplication::exit(code);
    };
    auto state = [] { return FFRecorder::getInstance().captureScheduler().state(); };
    auto pollUntil = [](std::function<bool()> done, int timeoutMs,
                        std::function<void()> onDone, std::function<void()> onTimeout) {
      auto timer = std::make_shared<QElapsedTimer>();
      timer->start();
      auto poller = std::make_shared<std::function<void()>>();
      *poller = [done, timeoutMs, onDone, onTimeout, timer, poller]() {
        if (done()) { onDone(); return; }
        if (timer->elapsed() >= timeoutMs) { onTimeout(); return; }
        QTimer::singleShot(100, [poller] { (*poller)(); });
      };
      QTimer::singleShot(100, [poller] { (*poller)(); });
    };
    auto &recorder = FFRecorder::getInstance();
    auto &scheduler = recorder.captureScheduler();
    const QString requestedMic = qEnvironmentVariable("SUBREC_AUTOTEST_MIC_DEVICE").trimmed();
    QString cycleMic = scheduler.selectedMicrophoneId();
    if (!requestedMic.isEmpty()) cycleMic.clear();
    if (!requestedMic.isEmpty()) {
      QString exact;
      QStringList partial;
      for (const QVariant &value : scheduler.devices()) {
        const QVariantMap device = value.toMap();
        if (device.value(QStringLiteral("kind")).toString() != QLatin1String("microphone")) continue;
        const QString id = device.value(QStringLiteral("id")).toString();
        const QString name = device.value(QStringLiteral("displayName")).toString();
        if (id.compare(requestedMic, Qt::CaseInsensitive) == 0
            || name.compare(requestedMic, Qt::CaseInsensitive) == 0) exact = id;
        else if (id.contains(requestedMic, Qt::CaseInsensitive)
                 || name.contains(requestedMic, Qt::CaseInsensitive)) partial << id;
      }
      cycleMic = !exact.isEmpty() ? exact : (partial.size() == 1 ? partial.first() : QString());
    }
    QFileInfo baseFile(recorder.outputPath() + QLatin1Char('/') + recorder.outputFileName());
    const QString baseStem = baseFile.completeBaseName();
    const QString baseSuffix = baseFile.suffix().isEmpty() ? QStringLiteral("mp4") : baseFile.suffix();
    auto applyMode = [&scheduler, cycleMic](const QString &mode) {
      bool ok = false;
      if (mode == QLatin1String("both")) {
        ok = scheduler.setAudioMode(QStringLiteral("system"));
        if (ok) ok = cycleMic.isEmpty() ? scheduler.setAudioMode(QStringLiteral("both"))
                                       : scheduler.selectMixMicrophone(cycleMic);
      } else {
        if (!cycleMic.isEmpty())
          ok = scheduler.selectMicrophoneDevice(cycleMic);
        else
          ok = true;
        if (ok) ok = scheduler.setAudioMode(mode);
      }
      return ok;
    };
    auto step = std::make_shared<std::function<void(int)>>();
    *step = [step, modes, recordMs, baseStem, baseSuffix, &recorder, &scheduler,
             state, pollUntil, applyMode, exitWith](int index) mutable {
      if (index >= modes.size()) { exitWith(AutoTestOk); return; }
      const QString mode = modes.at(index);
      if (mode != QLatin1String("system") && mode != QLatin1String("microphone")
          && mode != QLatin1String("both") && mode != QLatin1String("none")) {
        qWarning() << "[AutoTest] invalid cycle mode =" << mode;
        exitWith(AutoTestBadEnvironment);
        return;
      }
      const QString fileName = QStringLiteral("%1_%2_%3.%4")
                                   .arg(baseStem).arg(index).arg(mode).arg(baseSuffix);
      recorder.setOutputFileName(fileName);
      QFile::remove(recorder.outputPath() + QLatin1Char('/') + fileName);
      if (!applyMode(mode)) {
        qWarning() << "[AutoTest] cycle mode could not be applied:" << scheduler.lastError();
        exitWith(AutoTestBadEnvironment);
        return;
      }
      const QString actualMic = scheduler.mixMicrophoneId().isEmpty()
                                    ? scheduler.selectedMicrophoneId() : scheduler.mixMicrophoneId();
      qInfo().noquote() << QStringLiteral("[AutoTest] audio mode = %1 mic=%2")
                               .arg(mode, actualMic.isEmpty() ? QStringLiteral("-") : actualMic);
      QTimer::singleShot(100, [step, index, mode, fileName, recordMs, state, pollUntil,
                               exitWith, &scheduler, &recorder] {
        startRecording();
        pollUntil(
            [state] { return state() == CaptureSchedulerState::Running
                           || state() == CaptureSchedulerState::Failed; },
            20000,
            [step, index, mode, fileName, recordMs, state, pollUntil, exitWith, &scheduler, &recorder] {
              if (state() != CaptureSchedulerState::Running) {
                qWarning() << "[AutoTest] result=FAIL cycle start failed:" << scheduler.lastError();
                exitWith(AutoTestNotRunning);
                return;
              }
              static bool runningRejectionChecked = false;
              if (!runningRejectionChecked) {
                runningRejectionChecked = true;
                const bool rejected = !scheduler.setAudioMode(QStringLiteral("none"));
                qInfo() << "[AutoTest] setAudioMode while Running rejected =" << (rejected ? "yes" : "no");
                if (!rejected) { exitWith(AutoTestBadEnvironment); return; }
              }
              QTimer::singleShot(recordMs, [step, index, mode, fileName, state, pollUntil, exitWith,
                                             &recorder] {
                stopRecording();
                pollUntil(
                    [state] { return state() == CaptureSchedulerState::Idle; },
                    20000,
                    [step, index, mode, fileName, exitWith, &recorder] {
                      const QFileInfo file(recorder.outputPath() + QLatin1Char('/') + fileName);
                  const auto result = autoTestRecordingResult(file.exists(), file.size(), recorder.captureError().isEmpty());
                  const bool ok = result == AutoTestOk;
                      qInfo().noquote() << QStringLiteral("[AutoTest] cycle %1 mode=%2 file=%3 result=%4 captureError=%5")
                                               .arg(index).arg(mode, file.absoluteFilePath(),
                                                               ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"), recorder.captureError());
                      if (!ok) { exitWith(result); return; }
                      (*step)(index + 1);
                    },
                    [exitWith] { exitWith(AutoTestStopTimeout); });
              });
            },
            [exitWith] { exitWith(AutoTestNotRunning); });
      });
    };
    QTimer::singleShot(0, [step] { (*step)(0); });
  } else if (scenario == "pause_cycles") {
#if !defined(SUBREC_STRIP_TEST_HOOKS) || !SUBREC_STRIP_TEST_HOOKS
    // Pause-cycle acceptance scenario. RECORD_SECONDS is the public duration
    // knob; retain RECORD_MS as a compatibility fallback for existing scripts.
    const int secondsEnv = qEnvironmentVariableIntValue("SUBREC_AUTOTEST_RECORD_SECONDS");
    const int recordMsFallback = qEnvironmentVariableIntValue("SUBREC_AUTOTEST_RECORD_MS");
    const int recordMs = secondsEnv > 0
                             ? secondsEnv * 1000
                             : (recordMsFallback > 0 ? recordMsFallback : 5000);
    const int cyclesEnv = qEnvironmentVariableIntValue("SUBREC_AUTOTEST_PAUSE_CYCLES");
    const int cycles = cyclesEnv > 0 ? cyclesEnv : 1;
    const int pauseMsEnv = qEnvironmentVariableIntValue("SUBREC_AUTOTEST_PAUSE_MS");
    const int pauseMs = pauseMsEnv > 0 ? pauseMsEnv : 10000;
    const int interval = std::max(1, recordMs / (cycles + 1));
    const int stopAfterMs = std::max(recordMs, interval * cycles + pauseMs + 100);
    if (quitDelay <= 0)
      finalQuitDelay = stopAfterMs + 60000;
    auto exitWith = [quitIssued](int code) {
      if (quitIssued->exchange(true, std::memory_order_acq_rel)) return;
      qInfo() << "[AutoTest] exit code" << code;
      const auto grace = capture_stop::timeout() + std::chrono::milliseconds(1500);
      capture_stop::armAutoTestExitWatchdog(code, grace);
      QCoreApplication::exit(code);
    };
    auto state = [] { return FFRecorder::getInstance().captureScheduler().state(); };
    auto pollUntil = [](std::function<bool()> done, int timeoutMs,
                        std::function<void()> onDone, std::function<void()> onTimeout) {
      auto timer = std::make_shared<QElapsedTimer>();
      timer->start();
      auto poller = std::make_shared<std::function<void()>>();
      *poller = [done, timeoutMs, onDone, onTimeout, timer, poller]() {
        if (done()) { onDone(); return; }
        if (timer->elapsed() >= timeoutMs) { onTimeout(); return; }
        QTimer::singleShot(100, [poller] { (*poller)(); });
      };
      QTimer::singleShot(100, [poller] { (*poller)(); });
    };
    schedule(200, [] { startRecording(); });
    pollUntil(
        [state] { return state() == CaptureSchedulerState::Running
                       || state() == CaptureSchedulerState::Failed; },
        20000,
        [state, schedule, pollUntil, exitWith, cycles, pauseMs, interval, stopAfterMs] {
          if (state() != CaptureSchedulerState::Running) {
            qWarning() << "[AutoTest] result=FAIL stage=start";
            exitWith(AutoTestNotRunning);
            return;
          }
          for (int i = 0; i < cycles; ++i) {
            const int pauseAt = interval * (i + 1);
            schedule(pauseAt, [i, pauseMs] {
              auto &r = FFRecorder::getInstance();
              if (r.captureScheduler().state() != CaptureSchedulerState::Running) return;
              qInfo() << "[AutoTest] pause" << (i + 1)
                      << "at mediaUs/wall" << r.mediaClock().nowMediaUs()
                      << av_gettime_relative();
              pauseRecording();
              QTimer::singleShot(pauseMs, [i] {
                auto &r = FFRecorder::getInstance();
                if (r.captureScheduler().state() != CaptureSchedulerState::Paused) return;
                qInfo() << "[AutoTest] resume" << (i + 1)
                        << "at mediaUs/wall" << r.mediaClock().nowMediaUs()
                        << av_gettime_relative();
                readyRecording();
              });
            });
          }
          schedule(stopAfterMs, [state, pollUntil, exitWith] {
            stopRecording();
            pollUntil(
                [state] { return state() == CaptureSchedulerState::Idle; },
                20000,
                [exitWith] {
                  auto &recorder = FFRecorder::getInstance();
                  const QFileInfo file(recorder.outputPath() + "/" + recorder.outputFileName());
                  const auto result = autoTestRecordingResult(file.exists(), file.size(), recorder.captureError().isEmpty());
                  logAutoTestRecordingResult(result);
                  exitWith(result);
                },
                [exitWith] { exitWith(AutoTestStopTimeout); });
          });
        },
        [exitWith] { exitWith(AutoTestNotRunning); });
#else
    qDebug() << "[AutoTest] pause_cycles is disabled in SUBREC_STRIP_TEST_HOOKS builds";
    QTimer::singleShot(0, [quitIssued] {
      if (!quitIssued->exchange(true, std::memory_order_acq_rel))
        QCoreApplication::exit(AutoTestBadEnvironment);
    });
#endif
  } else if (scenario == "record_seconds") {
    // Start, record SUBREC_AUTOTEST_RECORD_MS (default 5000) once Running, stop, wait for Idle
    // and check the output file. The process exit code is the result (enum AutoTestExit).
    // Combine with SUBREC_CAPTURE_BACKEND, SUBREC_AUTOTEST_MIC_DEVICE, SUBREC_AUTOTEST_OUTPUT.
    const int recordMsEnv = qEnvironmentVariableIntValue("SUBREC_AUTOTEST_RECORD_MS");
    const int recordMs = recordMsEnv > 0 ? recordMsEnv : 5000;
    if (quitDelay <= 0)
      finalQuitDelay = recordMs + 60000;
    auto exitWith = [quitIssued](int code) {
      if (quitIssued->exchange(true, std::memory_order_acq_rel))
        return;
      qInfo() << "[AutoTest] exit code" << code;
      const auto grace = capture_stop::timeout() + std::chrono::milliseconds(1500);
      capture_stop::armAutoTestExitWatchdog(code, grace);
      QCoreApplication::exit(code);
    };
    // Polls every 100 ms until done() or timeoutMs, then calls onDone or onTimeout.
    auto pollUntil = [](std::function<bool()> done, int timeoutMs,
                        std::function<void()> onDone, std::function<void()> onTimeout) {
      auto timer = std::make_shared<QElapsedTimer>();
      timer->start();
      auto poller = std::make_shared<std::function<void()>>();
      *poller = [done, timeoutMs, onDone, onTimeout, timer, poller]() {
        if (done()) { onDone(); return; }
        if (timer->elapsed() >= timeoutMs) { onTimeout(); return; }
        QTimer::singleShot(100, [poller] { (*poller)(); });
      };
      QTimer::singleShot(100, [poller] { (*poller)(); });
    };
    auto state = [] { return FFRecorder::getInstance().captureScheduler().state(); };

    schedule(200, [] { startRecording(); });
    pollUntil(
        [state] {
          return state() == CaptureSchedulerState::Running
              || state() == CaptureSchedulerState::Failed;
        },
        20000,
        [state, schedule, pollUntil, exitWith, recordMs] {
          if (state() != CaptureSchedulerState::Running) {
            qWarning().noquote() << "[AutoTest] result=FAIL stage=start error="
                                        + FFRecorder::getInstance().captureScheduler().lastError();
            exitWith(AutoTestNotRunning);
            return;
          }
          qInfo() << "[AutoTest] running, recording" << recordMs << "ms";
          schedule(recordMs, [state, pollUntil, exitWith] {
            stopRecording();
            pollUntil(
                [state] { return state() == CaptureSchedulerState::Idle; },
                20000,
                [exitWith] {
                  auto &recorder = FFRecorder::getInstance();
                  const QFileInfo file(recorder.outputPath() + "/" + recorder.outputFileName());
                  const auto result = autoTestRecordingResult(file.exists(), file.size(), recorder.captureError().isEmpty());
                  logAutoTestRecordingResult(result);
                  exitWith(result);
                },
                [exitWith] {
                  qWarning() << "[AutoTest] result=FAIL stage=stop (no Idle within 20 s)";
                  exitWith(AutoTestStopTimeout);
                });
          });
        },
        [exitWith] {
          qWarning() << "[AutoTest] result=FAIL stage=start (no Running within 20 s)";
          exitWith(AutoTestNotRunning);
        });
    schedule(finalQuitDelay - 100, [exitWith] {
      qWarning() << "[AutoTest] result=FAIL stage=safety-timeout";
      exitWith(AutoTestStopTimeout);
    });
  } else {
    qDebug() << "[AutoTest] unknown scenario";
  }

  schedule(finalQuitDelay, requestQuit);
}

class QmlBridge : public QObject
{
  Q_OBJECT
public:
  explicit QmlBridge(QObject *parent = nullptr) : QObject(parent) {}

public slots:
  void onStopRecording() { if (g_shell) g_shell->requestStop(); }
  void onPauseRecording() { if (g_shell) g_shell->requestPause(); }
  void onStartRecording() { if (g_shell) g_shell->requestStart(); }
  void onReadyRecording() { if (g_shell) g_shell->requestResume(); }
  void onOpenScreen() { openSourceByControl(true); }
  void onOpenCamera() { openSourceByControl(false); }
  void onOpenSystemAudio() { openAudioByControl(true); }
  void onOpenMicrophone() { openAudioByControl(false); }
};

int main(int argc, char *argv[])
{
  qInstallMessageHandler(qtLogHandler);
  const bool debugLog = subrec::logging::configure(argc, argv);
  QApplication app(argc, argv); // Widgets: QSystemTrayIcon/QMenu need QApplication (§4.1, R9)
  subrec::logging::reportLevel(debugLog);
  // The native Windows style rejects the custom control delegates used by the QML pages.
  // Basic is deterministic across platforms and keeps Browse/style customization working.
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  int exitCode = 0;
  const bool autoTestMode = capture::test::autoTestScenarioActive();
  const bool startMinimizedArg = app.arguments().contains(QStringLiteral("--minimized"));

  // Single instance: before any device, recorder or window is created (§3.8, §4.6).
  // AutoTest skips it so parallel/automated runs are unaffected.
  std::unique_ptr<subrec::shell::SingleInstance> singleInstance;
  if (!autoTestMode) {
    singleInstance = std::make_unique<subrec::shell::SingleInstance>();
    if (singleInstance->acquire() == subrec::shell::SingleInstance::Role::Secondary) {
      using Request = subrec::shell::SingleInstance::Request;
      const bool acked = singleInstance->notifyPrimary(
          startMinimizedArg ? Request::Noop : Request::Show, 1000);
      if (!acked) {
        qWarning() << "[Shell] primary instance did not respond";
        QMessageBox::warning(nullptr, QStringLiteral("字录"),
            QStringLiteral("字录已经在运行，但没有响应。请在任务管理器中结束后重试。"));
      }
      return 0;
    }
    singleInstance->listen();
  }

  init();

  // Shell objects (GUI thread). Declared before the QML engine so they outlive it.
  subrec::shell::CaptureExclusion exclusion;
  subrec::shell::PowerRequest power;
  // M2: AutoTest uses a fresh temporary INI store (defaults, never touches HKCU ...\ui),
  // so a tester's manual C4/C6/C7 choices cannot change AutoTest results.
  std::unique_ptr<QTemporaryDir> autoTestSettingsDir;
  if (autoTestMode) {
    autoTestSettingsDir = std::make_unique<QTemporaryDir>();
    if (!autoTestSettingsDir->isValid()) {
      qCritical() << "[Shell] AutoTest: cannot create temporary settings dir; refusing to use user settings";
      return 3;
    }
    qInfo() << "[Shell] AutoTest shell settings store:" << autoTestSettingsDir->path();
  }
  const auto shellSettingsOwner = subrec::app::makeShellSettings(
      subrec::shell::CaptureExclusion::supported(),
      autoTestSettingsDir ? autoTestSettingsDir->path() : QString());
  subrec::shell::ShellSettings &shellSettings = *shellSettingsOwner;
  std::unique_ptr<subrec::shell::TrayController> tray;
  if (!autoTestMode && subrec::shell::TrayController::systemTrayAvailable())
    tray = std::make_unique<subrec::shell::TrayController>();
  subrec::shell::ShellStateSource shellState(FFRecorder::getInstance());
  subrec::shell::WindowController windowController(
      shellSettings, shellState, tray.get(), exclusion, power,
      subrec::shell::RecordingActions(
          engineStart, engineStop, enginePause, engineResume,
          [] {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(FFRecorder::getInstance().outputPath()));
          }));
  g_shell = &windowController;
  if (singleInstance)
    QObject::connect(singleInstance.get(), &subrec::shell::SingleInstance::showRequested,
                     &windowController, &subrec::shell::WindowController::showMainWindow);
  // C4 / --minimized only with a usable tray (otherwise there is no way back).
  const bool startHidden = (startMinimizedArg || shellSettings.startMinimized()) && tray
                           && shellSettings.trayEnabled()
                           && subrec::shell::TrayController::systemTrayAvailable();

  QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {
    qDebug() << "[App] aboutToQuit";
    PreviewBridge::instance().setPreviewItem(nullptr);
    FFRecorder::getInstance().captureScheduler().stop();
  });

  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);

  {
    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []()
        { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    // Register the recorder object with QML context before loading the module
    qmlRegisterType<FFGLItem>("GLPreview", 1, 0, "GLView");
    engine.rootContext()->setContextProperty("recorder",
                                             &FFRecorder::getInstance());
    engine.rootContext()->setContextProperty("shell", &windowController);
    engine.rootContext()->setContextProperty("shellSettings", &shellSettings);
    engine.rootContext()->setContextProperty("shellStartHidden", startHidden);
    engine.loadFromModule("SubRec", "Main");

    if (!engine.rootObjects().isEmpty())
    {
      auto root = engine.rootObjects().first();

      // 绑定预览项
      QObject *obj =
          root->findChild<QObject *>("previewView", Qt::FindChildrenRecursively);
      auto previewItem = qobject_cast<FFGLItem *>(obj);
      PreviewBridge::instance().setPreviewItem(previewItem);
      qDebug() << "PreviewItem set:" << previewItem;

      if (auto *mainWindow = qobject_cast<QQuickWindow *>(root)) {
        mainWindow->setObjectName(QStringLiteral("mainWindow"));
        windowController.attachMainWindow(mainWindow); // also registers capture exclusion
        if (startHidden) windowController.startHidden();
      } else {
        qWarning() << "[Shell] QML root is not a QQuickWindow; shell window control disabled";
      }

      static QmlBridge bridge;
      QObject::connect(root, SIGNAL(startRecording()), &bridge,
                       SLOT(onStartRecording()));
      QObject::connect(root, SIGNAL(stopRecording()), &bridge,
                       SLOT(onStopRecording()));
      QObject::connect(root, SIGNAL(pauseRecording()), &bridge,
                       SLOT(onPauseRecording()));
      QObject::connect(root, SIGNAL(readyRecording()), &bridge,
                       SLOT(onReadyRecording()));
      QObject::connect(root, SIGNAL(openScreen()), &bridge, SLOT(onOpenScreen()));
      QObject::connect(root, SIGNAL(openCamera()), &bridge, SLOT(onOpenCamera()));
      QObject::connect(root, SIGNAL(openSystemAudio()), &bridge,
                       SLOT(onOpenSystemAudio()));
      QObject::connect(root, SIGNAL(openMicrophone()), &bridge,
                       SLOT(onOpenMicrophone()));

      runAutoScenario(root);
    }
    else
    {
      qDebug() << "Failed to load QML root object";
    }

    exitCode = app.exec();
    qDebug() << "[App] app.exec returned" << exitCode;
    windowController.attachMainWindow(nullptr); // window dies with the engine
  }
  g_shell = nullptr;

  if (autoTestMode)
  {
    if (logFile.isOpen())
    {
      logFile.flush();
    }
    if (autoTestSettingsDir) autoTestSettingsDir->remove(); // _Exit skips destructors
    ::_Exit(exitCode);
  }

  PreviewBridge::instance().setPreviewItem(nullptr);
  FFRecorder::destoryInstance();
  return exitCode;
}
#include "main.moc"

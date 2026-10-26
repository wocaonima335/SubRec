#include "opengl/ffglitem.h"
#include "recorder/capture_scheduler.h"
#include "recorder/ffrecorder.h"


#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QRect>
#include <QQuickWindow>
#include <QTimer>
#include <QTimer>
#include <cstdlib>
#include <cstdio>
#include <memory>

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
    QString logPath = QCoreApplication::applicationDirPath() + "/SubRec.log";
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

void startRecording()
{
  qDebug() << "开始录制...";
  FFRecorder::getInstance().captureScheduler().start();
}

void stopRecording()
{
  qDebug() << "停止录制...";
  FFRecorder::getInstance().captureScheduler().stop();
}

void pauseRecording()
{
  FFRecorder::getInstance().captureScheduler().pause();
}

void readyRecording()
{
  FFRecorder::getInstance().captureScheduler().resume();
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

// Exit codes of autotest runs (tools/pc2_verify.ps1 checks them).
enum AutoTestExit
{
  AutoTestOk = 0,
  AutoTestBadEnvironment = 2, // SUBREC_AUTOTEST_MIC_DEVICE / _OUTPUT unusable
  AutoTestNotRunning = 3,     // capture never reached Running (or went Failed)
  AutoTestNoOutput = 4,       // stopped, but the output file is missing or empty
  AutoTestStopTimeout = 5     // capture did not return to Idle after stop
};

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

  const bool withSystem = qEnvironmentVariableIntValue("SUBREC_AUTOTEST_SYSTEM_AUDIO") == 1;
  if (withSystem) scheduler.changeAudioSource(true);
  const QString wanted = qEnvironmentVariable("SUBREC_AUTOTEST_MIC_DEVICE").trimmed();
  if (!wanted.isEmpty()) {
    if (!withSystem) scheduler.changeAudioSource(false);
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
    const QString chosen = !exact.isEmpty() ? exact : (partial.size() == 1 ? partial.first() : QString());
    if (chosen.isEmpty() || !(withSystem ? scheduler.selectMixMicrophone(chosen) : scheduler.selectAudioDevice(chosen))) {
      qWarning().noquote() << "[AutoTest] SUBREC_AUTOTEST_MIC_DEVICE=" + wanted
                                  + " matches no single available microphone; candidates: "
                                  + (candidates.isEmpty() ? QStringLiteral("none") : candidates.join(QStringLiteral(", ")));
      return false;
    }
    qInfo().noquote() << "[AutoTest] microphone =" << chosen;
  }
  return true;
}

void runAutoScenario(QObject *root)
{
  const QString scenario =
      qEnvironmentVariable("SUBREC_AUTOTEST_SCENARIO").trimmed();
  if (scenario.isEmpty())
    return;

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
                         requestQuit,
                         QStringLiteral("idle-after-phase4-long-dynamic-stop"));
                   });
        },
        QStringLiteral("running-before-phase4-long-dynamic"));
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
      QCoreApplication::exit(code);
      QTimer::singleShot(1500, [code] {
        qWarning() << "[AutoTest] quit timeout, force exit";
        ::_Exit(code);
      });
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
                  const bool ok = file.exists() && file.size() > 0;
                  qInfo().noquote()
                      << QStringLiteral("[AutoTest] result=%1 file=%2 bytes=%3 encoder=%4 "
                                        "hardware=%5 resolution=%6 encoderDetail=%7 captureError=%8")
                             .arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
                                  file.absoluteFilePath(),
                                  QString::number(file.exists() ? file.size() : -1),
                                  recorder.selectedVideoEncoder(),
                                  recorder.selectedVideoEncoderIsHardware() ? QStringLiteral("yes")
                                                                            : QStringLiteral("no"),
                                  recorder.videoResolution(),
                                  recorder.selectedVideoEncoderDetail(),
                                  recorder.captureError());
                  exitWith(ok ? AutoTestOk : AutoTestNoOutput);
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
  void onStopRecording() { stopRecording(); }
  void onPauseRecording() { pauseRecording(); }
  void onStartRecording() { startRecording(); }
  void onReadyRecording() { readyRecording(); }
  void onOpenScreen() { openSourceByControl(true); }
  void onOpenCamera() { openSourceByControl(false); }
  void onOpenSystemAudio() { openAudioByControl(true); }
  void onOpenMicrophone() { openAudioByControl(false); }
};

int main(int argc, char *argv[])
{
  qInstallMessageHandler(qtLogHandler);
  QGuiApplication app(argc, argv);
  int exitCode = 0;
  const bool autoTestMode =
      !qEnvironmentVariable("SUBREC_AUTOTEST_SCENARIO").trimmed().isEmpty();

  init();

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
  }

  if (autoTestMode)
  {
    if (logFile.isOpen())
    {
      logFile.flush();
    }
    ::_Exit(exitCode);
  }

  PreviewBridge::instance().setPreviewItem(nullptr);
  FFRecorder::destoryInstance();
  return exitCode;
}
#include "main.moc"

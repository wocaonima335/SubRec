#pragma once
// G: hardware scenario. Included only by the subtitle-enabled application.
#if defined(SUBREC_HAS_SUBTITLE)
#include "capture/autotest_result.h"
#include "capture/test_hook_gate.h"
#include "recorder/capture_scheduler.h"
#include "recorder/ffrecorder.h"
#include "subtitle/core/subtitle_writers.h"
#include "subtitle/service/subtitle_service.h"
#include "thread/failure_cleanup_deadline.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <memory>

inline void runSubtitleAutoTest(QObject *owner, SubtitleService *service,
                                std::function<void()> start, std::function<void()> stop)
{
    if (!capture::test::hooksEnabled() || !service || !service->available()
        || service->state() == QLatin1String("off")) {
        qWarning() << "[AutoTest] subtitle live requires an installed host/model and asr/mode=live";
        QTimer::singleShot(0, [] { QCoreApplication::exit(AutoTestBadEnvironment); });
        return;
    }
    struct State {
        enum Phase { Starting, Recording, Finishing } phase = Starting;
        QElapsedTimer elapsed;
        bool sawLine = false;
    };
    const auto state = std::make_shared<State>();
    state->elapsed.start();
    const int requested = qEnvironmentVariableIntValue("SUBREC_AUTOTEST_RECORD_MS");
    const int recordMs = requested > 0 ? requested : 15000;
    auto *timer = new QTimer(owner);
    timer->setInterval(100);
    const auto exitWith = [timer](int code) {
        timer->stop();
        qInfo() << "[AutoTest] record_seconds_subtitle_live result="
                << (code == AutoTestOk ? "PASS" : "FAIL") << "exit code" << code;
        capture_stop::armAutoTestExitWatchdog(code, capture_stop::timeout() + std::chrono::milliseconds(1500));
        QCoreApplication::exit(code);
    };
    QObject::connect(timer, &QTimer::timeout, owner,
                     [state, service = QPointer<SubtitleService>(service), stop, exitWith, recordMs] {
        if (!service) { exitWith(AutoTestBadEnvironment); return; }
        auto &recorder = FFRecorder::getInstance();
        const auto captureState = recorder.captureScheduler().state();
        if (!recorder.captureError().isEmpty() || captureState == CaptureSchedulerState::Failed
            || service->state() == QLatin1String("failed") || service->state() == QLatin1String("unavailable")) {
            exitWith(AutoTestCaptureError); return;
        }
        switch (state->phase) {
        case State::Starting:
            if (captureState == CaptureSchedulerState::Running) {
                state->phase = State::Recording;
                state->elapsed.restart();
            } else if (state->elapsed.elapsed() > 20000) exitWith(AutoTestNotRunning);
            break;
        case State::Recording:
            state->sawLine |= !service->currentLine().isEmpty();
            if (state->elapsed.elapsed() >= recordMs) {
                state->phase = State::Finishing;
                state->elapsed.restart();
                stop(); // existing shell stop action and its watchdog
            }
            break;
        case State::Finishing:
            if (captureState == CaptureSchedulerState::Idle && service->state() == QLatin1String("done")) {
                QFile srt(service->lastSrtPath());
                std::vector<subtitle::Cue> cues;
                const bool hasCues = srt.open(QIODevice::ReadOnly)
                    && subtitle::parseSrt(srt.readAll().toStdString(), cues) && !cues.empty();
                const QFileInfo video(recorder.outputPath() + "/" + recorder.outputFileName());
                const bool ok = state->sawLine && hasCues && video.exists() && video.size() > 0
                    && recorder.captureScheduler().subtitleDroppedBlocks() == 0;
                qInfo() << "[AutoTest] subtitle currentLine=" << state->sawLine << "cues=" << cues.size()
                        << "dropped=" << recorder.captureScheduler().subtitleDroppedBlocks();
                exitWith(ok ? AutoTestOk : AutoTestNoOutput);
            } else if (state->elapsed.elapsed() > 30000) exitWith(AutoTestStopTimeout);
            break;
        }
    });
    timer->start();
    QTimer::singleShot(200, owner, std::move(start));
}
#endif

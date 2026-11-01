#include "shell/window_controller.h"
#include "shell/shell_settings.h"
#include "shell/shell_state_source.h"
#include "shell/tray_controller.h"
#include "shell/capture_exclusion.h"
#include "shell/power_request.h"
#include "fakes/preview_bridge_double.h"
#include "fakes/observable_recorder.h"
#include "test_support.h"
#include <QApplication>
#include <QQuickWindow>
using namespace subrec::shell;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try {
        CHECK(argc == 2);
        IsolatedSettings isolated;
        const QString mode = QString::fromLocal8Bit(argv[1]);
        FFRecorder recorder;
        ShellStateSource source(recorder);
        ShellSettings settings(CaptureExclusion::supported());
        CaptureExclusion exclusion;
        PowerRequest power;
        // Observe the real platform APIs. Linux deliberately reports no exclusion
        // or power request; activation ordering still needs a Windows run.
        auto checkActive = [&](bool active) {
            CHECK(exclusion.effective() == (active && CaptureExclusion::supported()));
#ifdef _WIN32
            CHECK(power.active() == active);
#else
            CHECK(!CaptureExclusion::supported());
            CHECK(!power.active());
#endif
        };
        TrayController tray;
        WindowController *controller = nullptr;
        int starts = 0, stops = 0, pauses = 0, resumes = 0;
        std::function<bool()> start = [&] { recorder.setState("Preparing"); return true; };
        WindowController shell(settings, source, mode == "no-tray" ? nullptr : &tray, exclusion, power,
            RecordingActions([&] {
                checkActive(true); ++starts;
                controller->requestStart(); // Reentrant entry must not start or roll back.
                emit recorder.outputFileNameChanged(); // Unrelated Idle signal during startup.
                checkActive(true);
                return start();
            }, [&] { ++stops; recorder.setState("Stopping"); },
            [&] { ++pauses; recorder.setState("Paused"); }, [&] { ++resumes; recorder.setState("Running"); }, [] {}));
        controller = &shell;
        QQuickWindow window;
        window.setGeometry(40, 60, 640, 480);
        window.show(); shell.attachMainWindow(&window);
        CHECK(shell.windowShown()); CHECK(PreviewBridge::instance().previewWanted());
        if (mode == "start") {
            start = [&] { return false; };
            shell.requestStart(); CHECK(starts == 1); checkActive(false);
            start = [&] { recorder.setState("Preparing"); recorder.setState("Failed"); return false; };
            shell.requestStart(); CHECK(starts == 2); checkActive(false);
            start = [&] { recorder.setState("Preparing"); return true; };
            shell.requestStartStop(); CHECK(starts == 3); checkActive(true);
            shell.requestStart(); shell.requestStartStop(); CHECK(starts == 3 && stops == 0);
            recorder.setState("Running"); shell.requestStart(); CHECK(starts == 3);
            shell.requestStop(); checkActive(true);
            recorder.setState("Idle"); checkActive(false);
            // An accepted asynchronous start must reject duplicates until state arrives.
            start = [] { return true; };
            shell.requestStart(); shell.requestStart(); CHECK(starts == 4); checkActive(true);
            recorder.setState("Preparing"); recorder.setState("Failed"); checkActive(false);
        } else if (mode == "actions") {
            shell.requestPause(); shell.requestResume(); shell.requestStop(); CHECK(pauses == 0 && resumes == 0 && stops == 0);
            shell.requestStart(); recorder.setState("Running");
            shell.requestResume(); CHECK(resumes == 0);
            shell.requestPause(); shell.requestPause(); CHECK(pauses == 1);
            shell.requestPauseResume(); CHECK(resumes == 1);
            shell.requestPauseResume(); CHECK(pauses == 2);
            shell.requestStartStop(); CHECK(stops == 1);
            shell.requestStartStop(); shell.requestPauseResume(); CHECK(stops == 1 && pauses == 2);
            recorder.setState("Failed"); checkActive(false);
        } else if (mode == "visibility") {
            const auto geometry = window.geometry();
            shell.requestMinimize(); CHECK(window.visibility() == QWindow::Minimized);
            CHECK(!shell.windowShown() && !PreviewBridge::instance().previewWanted());
            shell.toggleWindow(); CHECK(shell.windowShown() && window.geometry() == geometry);
            shell.toggleWindow(); CHECK(window.visibility() == QWindow::Hidden);
            CHECK(!PreviewBridge::instance().previewWanted());
            shell.showMainWindow(); CHECK(PreviewBridge::instance().previewWanted());
            settings.setMinimizeToTray(true); window.showMinimized(); CHECK(window.visibility() == QWindow::Hidden);
            shell.showMainWindow(); window.showMaximized(); shell.hideToTray(); shell.showMainWindow();
            CHECK(window.visibility() == QWindow::Maximized);
            shell.hideToTray(); settings.setTrayEnabled(false); CHECK(shell.windowShown());
            shell.hideToTray(); CHECK(window.visibility() == QWindow::Minimized);
            shell.showMainWindow(); settings.setTrayEnabled(true); shell.startHidden(); CHECK(window.visibility() == QWindow::Hidden);
        } else if (mode == "close") {
            int choices = 0, confirms = 0, variant = -1;
            QObject::connect(&shell, &WindowController::closeChoiceRequested, [&](bool, const QString &) { ++choices; });
            QObject::connect(&shell, &WindowController::exitConfirmRequested, [&](int v, const QString &) {
                CHECK(shell.windowShown()); ++confirms; variant = v;
            });
            shell.requestClose(); shell.requestClose(); CHECK(choices == 1); shell.cancelExit();
            shell.requestClose(); CHECK(choices == 2);
            shell.resolveCloseChoice(true, true); CHECK(settings.closeAction() == CloseAction::Tray && !shell.windowShown());
            shell.showMainWindow(); shell.requestStart(); recorder.setState("Running");
            shell.requestClose(); CHECK(confirms == 0 && !shell.windowShown());
            shell.beginExit(); CHECK(confirms == 1 && variant == 0); shell.beginExit(); CHECK(confirms == 1);
            shell.cancelExit(); recorder.setState("Stopping"); shell.beginExit(); CHECK(confirms == 2 && variant == 3);
            shell.cancelExit(); settings.setCloseActionText("exit"); shell.requestClose(); CHECK(confirms == 3);
        } else if (mode == "no-tray") {
            shell.startHidden(); CHECK(shell.windowShown());
            settings.setMinimizeToTray(true); shell.requestMinimize(); CHECK(window.visibility() == QWindow::Minimized);
            shell.showMainWindow(); settings.setHideOnRecordStart(true); shell.requestStart();
            CHECK(window.visibility() == QWindow::Minimized); checkActive(true);
            int choices = 0, confirms = 0;
            QObject::connect(&shell, &WindowController::closeChoiceRequested, [&] { ++choices; });
            QObject::connect(&shell, &WindowController::exitConfirmRequested, [&] { ++confirms; });
            shell.requestClose(); CHECK(choices == 0 && confirms == 1 && shell.windowShown());
        } else if (mode == "source") {
            int changes = 0;
            QObject::connect(&source, &ShellStateSource::changed, [&] { ++changes; });
            auto s = source.snapshot(); CHECK(s.state == RecState::Idle && s.outputFile == "/recordings/example.mp4");
            CHECK(!s.subtitleFinishing && s.subtitleProgress == 0 && s.window == WindowVisibility::Shown);
            recorder.devices = {QVariantMap{{"kind", "microphone"}, {"available", true}},
                QVariantMap{{"kind", "camera"}, {"available", false}}};
            emit recorder.captureDevicesChanged(); CHECK(source.snapshot().state == RecState::NoDevice);
            recorder.devices.append(QVariantMap{{"kind", "camera"}, {"available", true}});
            emit recorder.captureDevicesChanged(); CHECK(source.snapshot().state == RecState::Idle);
            recorder.time = "00:22:00"; emit recorder.captureTimeTextChanged(); CHECK(source.snapshot().timeText == recorder.time);
            recorder.error = "failure"; emit recorder.captureErrorChanged(); CHECK(source.snapshot().captureError == recorder.error);
            recorder.path = "/tmp"; emit recorder.outputPathChanged();
            recorder.file = "new.mp4"; emit recorder.outputFileNameChanged(); CHECK(source.snapshot().outputFile == "/tmp/new.mp4");
            recorder.setState("Failed"); CHECK(changes == 7 && source.snapshot().state == RecState::Failed);
            settings.setExcludeText("always"); CHECK(exclusion.effective() == CaptureExclusion::supported());
        } else CHECK(false);
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

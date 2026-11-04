#include <QtTest>
#include "ui/backend/shell_view.h"
#include "shell/shell_settings.h"
#include "shell/window_controller.h"
#include "shell/shell_state_source.h"
#include "shell/capture_exclusion.h"
#include "shell/power_request.h"
#include "tests/shell/fakes/observable_recorder.h"
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QSettings>
using namespace subrec::ui;
using namespace subrec::shell;

class ShellViewTests : public QObject {
    Q_OBJECT
private slots:

    void defaultsAndSettings()
    {
        ShellView view(nullptr);
        QVERIFY(view.windowShown());
        QVERIFY(view.trayEnabled());
        QVERIFY(!view.minimizeToTray());
        QCOMPARE(view.closeAction(), QString("ask"));
        QCOMPARE(view.floatingBar(), QString("whenHidden"));
        QVERIFY(!view.excludeSupported());
        QTemporaryDir directory;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        ShellSettings settings(true);
        view.attach(nullptr, &settings, true);
        QSignalSpy changed(&view, &ShellView::settingsChanged);
        const QList<QPair<QByteArray, QVariant>> values = {
            {"trayEnabled", false}, {"minimizeToTray", true}, {"closeAction", "exit"},
            {"startMinimized", true}, {"floatingBar", "always"}, {"excludeFromCapture", "always"},
            {"hideOnRecordStart", true}, {"confirmExitWhileRecording", false}
        };
        for (const auto &entry : values) {
            QVERIFY(view.setProperty(entry.first, entry.second));
            QCOMPARE(settings.property(entry.first), view.property(entry.first));
        }
        QCOMPARE(changed.count(), values.size());
        ShellSettings restored(true);
        for (const auto &entry : values)
            QCOMPARE(restored.property(entry.first), view.property(entry.first));
        view.attach(nullptr, nullptr, false);
        view.requestClose();
        view.requestStart();
        view.requestPause();
        view.requestResume();
        view.requestStop();
        view.requestStartStop();
        view.requestPauseResume();
        view.requestMinimize();
        view.resolveCloseChoice(false, false);
        view.confirmExit();
        view.cancelExit();
        view.showMainWindow();
    }
    void requestsAndSignals()
    {
        QTemporaryDir directory;
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        FFRecorder recorder;
        ShellStateSource source(recorder);
        ShellSettings settings(false);
        CaptureExclusion exclusion;
        PowerRequest power;
        int starts = 0, stops = 0, pauses = 0, resumes = 0;
        WindowController controller(settings, source, nullptr, exclusion, power,
            RecordingActions([&] { ++starts; recorder.setState("Preparing"); return true; },
                [&] { ++stops; recorder.setState("Stopping"); },
                [&] { ++pauses; recorder.setState("Paused"); },
                [&] { ++resumes; recorder.setState("Running"); }, [] {}));
        QQuickWindow window;
        controller.attachMainWindow(&window);
        ShellView view(nullptr);
        view.attach(&controller, &settings, false);
        QSignalSpy rejected(&view, &ShellView::startRejected);
        QSignalSpy pages(&view, &ShellView::pageRequested);
        QSignalSpy close(&view, &ShellView::closeChoiceRequested);
        QSignalSpy exit(&view, &ShellView::exitConfirmRequested);
        QSignalSpy settingsPage(&view, &ShellView::settingsPageRequested);
        emit controller.startRejected("reason");
        emit controller.pageRequested("library");
        emit controller.closeChoiceRequested(false, "00:00:00");
        emit controller.exitConfirmRequested(0, "00:00:00");
        emit controller.settingsPageRequested();
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(pages.count(), 1);
        QCOMPARE(close.count(), 1);
        QCOMPARE(exit.count(), 1);
        QCOMPARE(settingsPage.count(), 1);
        view.requestStart();
        QCOMPARE(starts, 1);
        recorder.setState("Running");
        view.requestPause();
        QCOMPARE(pauses, 1);
        view.requestResume();
        QCOMPARE(resumes, 1);
        view.requestPauseResume();
        QCOMPARE(pauses, 2);
        view.requestPauseResume();
        QCOMPARE(resumes, 2);
        view.requestStop();
        QCOMPARE(stops, 1);
        recorder.setState("Idle");
        view.requestStartStop();
        QCOMPARE(starts, 2);
        view.showMainWindow();
        QVERIFY(view.windowShown());
        view.requestMinimize();
        QCOMPARE(window.visibility(), QWindow::Minimized);
    }

};

QTEST_MAIN(ShellViewTests)
#include "shell_view_tests.moc"

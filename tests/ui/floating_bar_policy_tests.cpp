#include <QtTest>
#include "shell/floating_bar_policy.h"
#include "shell/shell_logic.h"
#include "shell/shell_settings.h"
#include <QTemporaryDir>
#include <QSettings>
using namespace subrec::shell;

class FloatingBarPolicyTests : public QObject {
    Q_OBJECT
private slots:
    void settingsHintsAndPositions()
    {
        QTemporaryDir directory;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        ShellSettings settings(true);
        FloatingBarPolicy policy;
        policy.setSettings(&settings);
        ShellSnapshot snapshot;
        snapshot.state = RecState::Running;
        snapshot.window = WindowVisibility::HiddenToTray;
        policy.setSnapshot(snapshot);
        QVERIFY(policy.shouldShow());
        QVERIFY(policy.hintPending());
        policy.acknowledgeHint();
        QVERIFY(settings.barHintShown());
        QVERIFY(!policy.hintPending());
        policy.savePosition("monitor", QPoint(-300, 40));
        QCOMPARE(policy.savedPosition("monitor"), std::optional<QPoint>(QPoint(-300, 40)));
        settings.setFloatingBarText("never");
        QVERIFY(!policy.shouldShow());
        policy.setTrayOverride(true);
        QVERIFY(policy.shouldShow());
        snapshot.state = RecState::Idle;
        policy.setSnapshot(snapshot);
        QVERIFY(!policy.shouldShow());
        snapshot.state = RecState::Running;
        policy.setSnapshot(snapshot);
        QVERIFY(!policy.shouldShow());
    }

    void decisionMatrix()
    {
        for (const auto state : {RecState::NoDevice, RecState::Idle, RecState::Preparing, RecState::Running,
                                 RecState::Paused, RecState::Stopping, RecState::Failed}) {
            for (const auto mode : {FloatingBarMode::Never, FloatingBarMode::WhenHidden, FloatingBarMode::Always}) {
                for (const auto window : {WindowVisibility::Shown, WindowVisibility::Minimized, WindowVisibility::HiddenToTray}) {
                    for (int override = -1; override <= 1; ++override) {
                        for (bool dismissed : {false, true}) {
                            FloatingBarInputs in;
                            in.state = state;
                            in.mode = mode;
                            in.window = window;
                            in.dismissedAfterError = dismissed;
                            if (override >= 0)
                                in.trayOverride = override != 0;
                            bool expected = mode != FloatingBarMode::Never
                                && (mode == FloatingBarMode::Always || window != WindowVisibility::Shown)
                                && (isCapturing(state) || (state == RecState::Failed && !dismissed));
                            if (isCapturing(state) && override >= 0)
                                expected = override != 0;
                            QCOMPARE(floatingBarShouldShow(in), expected);
                        }
                    }
                }
            }
        }
    }
    void savedAndFailedLifetime()
    {
        FloatingBarPolicy policy;
        policy.setMode(FloatingBarMode::Always);
        ShellSnapshot snapshot;
        snapshot.state = RecState::Stopping;
        policy.setSnapshot(snapshot);
        QVERIFY(policy.shouldShow());
        QVERIFY(!policy.hintPending());
        snapshot.state = RecState::Idle;
        policy.setSnapshot(snapshot);
        QCOMPARE(policy.barState(), QString("saved"));
        QVERIFY(policy.shouldShow());
        QTRY_VERIFY_WITH_TIMEOUT(!policy.shouldShow(), 2500);
        snapshot.state = RecState::Running;
        policy.setSnapshot(snapshot);
        snapshot.state = RecState::Failed;
        policy.setSnapshot(snapshot);
        QVERIFY(policy.shouldShow());
        policy.dismiss();
        QVERIFY(!policy.shouldShow());
        policy.setMode(FloatingBarMode::Never);
        snapshot.state = RecState::Running;
        policy.setSnapshot(snapshot);
        snapshot.state = RecState::Failed;
        policy.setSnapshot(snapshot);
        QVERIFY(!policy.shouldShow());
    }

};

QTEST_MAIN(FloatingBarPolicyTests)
#include "floating_bar_policy_tests.moc"

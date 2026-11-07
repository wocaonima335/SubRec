#include <QtTest>
#include "ui/backend/hotkey_controller.h"
#include "ui/backend/settings_store.h"
#include "shell/global_hotkeys.h"
#include <QQuickWindow>
#include <QTemporaryDir>
using namespace subrec::ui;
using subrec::shell::HotkeyAction;
using subrec::shell::RegisterResult;

namespace {

// Windows virtual-key codes for the synthesized presses. The Windows eventFilter reads
// nativeVirtualKey(); other platforms map the Qt key code, so every press carries both.
constexpr quint32 kVkBackspace = 0x08;
constexpr quint32 kVkEscape = 0x1B;
constexpr quint32 kVkR = 0x52;
constexpr quint32 kVkF9 = 0x78;

// Status text a live (non-empty) binding must show. Off Windows nothing is registered:
// "unsupported". On Windows the chord went to RegisterHotKey, so only Ok ("ok") or
// Conflict ("conflict") are acceptable; Unsupported there means the message window was
// not created and fails the test. Returns "" for any other result.
QString liveStatusText(RegisterResult result)
{
#ifdef Q_OS_WIN
    if (result == RegisterResult::Ok)
        return QStringLiteral("ok");
    if (result == RegisterResult::Conflict)
        return QStringLiteral("conflict");
#else
    if (result == RegisterResult::Unsupported)
        return QStringLiteral("unsupported");
#endif
    return {};
}

#ifdef Q_OS_WIN
constexpr const char *kLiveExpectation = "Ok or Conflict; Unsupported = message window not created";
#else
constexpr const char *kLiveExpectation = "Unsupported";
#endif

} // namespace

class HotkeyControllerTests : public QObject {
    Q_OBJECT
private slots:

    void captureAndPersistence()
    {
        QTemporaryDir directory;
        setUiSettingsIniPath(directory.filePath("ui.ini"));
        QQuickWindow window;
        subrec::shell::GlobalHotkeys hotkeys;
        HotkeyController controller(nullptr);
        controller.attach(&hotkeys, &window);
        // Empty when the controller shows the live result GlobalHotkeys reports for the
        // action; otherwise a message with the actual values.
        auto statusProblem = [&](const char *name, HotkeyAction action,
                                 const QString &shown) -> QByteArray {
            const RegisterResult raw = hotkeys.status(action);
            const QString expected = liveStatusText(raw);
            if (expected.isEmpty()) {
                return QByteArray(name) + ": GlobalHotkeys::status() = " + QByteArray::number(int(raw))
                    + " (Ok=0 Unset=1 Conflict=2 Invalid=3 Unsupported=4), controller shows \""
                    + shown.toUtf8() + "\"; expected " + kLiveExpectation;
            }
            if (shown != expected) {
                return QByteArray(name) + ": controller shows \"" + shown.toUtf8()
                    + "\" but GlobalHotkeys::status() = " + QByteArray::number(int(raw))
                    + " (\"" + expected.toUtf8() + "\")";
            }
            return QByteArray();
        };
        QCOMPARE(controller.shortcutLabel(0), QString("Ctrl+Alt+R"));
        QCOMPARE(controller.toggleWindowStatus(), QString("unset"));
        QByteArray problem = statusProblem("ToggleRecord", HotkeyAction::ToggleRecord,
                                           controller.toggleRecordStatus());
        QVERIFY2(problem.isEmpty(), problem.constData());
        problem = statusProblem("TogglePause", HotkeyAction::TogglePause, controller.togglePauseStatus());
        QVERIFY2(problem.isEmpty(), problem.constData());
        // Startup conflicts are exactly the actions whose registration failed (none off Windows).
        const int conflicts = int(controller.toggleRecordStatus() == QLatin1String("conflict"))
            + int(controller.togglePauseStatus() == QLatin1String("conflict"));
        QCOMPARE(int(controller.startupConflicts().size()), conflicts);
        QSignalSpy finished(&controller, &HotkeyController::captureFinished);
        auto key = [&](int code, Qt::KeyboardModifiers mods, quint32 nativeVirtualKey) {
            QKeyEvent event(QEvent::KeyPress, code, mods, 0, nativeVirtualKey, 0);
            QCoreApplication::sendEvent(&window, &event);
        };
        controller.beginCapture(2);
        QVERIFY(hotkeys.suspended());
        key(Qt::Key_R, Qt::ControlModifier | Qt::AltModifier, kVkR);
        QCOMPARE(finished.last().at(1).toInt(), int(HotkeyController::Duplicate));
        controller.beginCapture(2);
        key(Qt::Key_R, Qt::NoModifier, kVkR);
        QCOMPARE(finished.last().at(1).toInt(), int(HotkeyController::Invalid));
        controller.beginCapture(2);
        key(Qt::Key_F9, Qt::NoModifier, kVkF9);
        problem = statusProblem("ToggleWindow (F9)", HotkeyAction::ToggleWindow,
                                controller.toggleWindowStatus());
        QVERIFY2(problem.isEmpty(), problem.constData());
        // A lone F-key saves with the notice unless its registration failed (Conflict).
        const int f9Result = controller.toggleWindowStatus() == QLatin1String("conflict")
            ? int(HotkeyController::Conflict) : int(HotkeyController::SavedWithNotice);
        QCOMPARE(finished.last().at(1).toInt(), f9Result);
        QCOMPARE(controller.shortcutLabel(2), QString("F9"));
        QVERIFY(!hotkeys.suspended());
        controller.beginCapture(2);
        key(Qt::Key_Escape, Qt::NoModifier, kVkEscape);
        QCOMPARE(finished.last().at(1).toInt(), int(HotkeyController::Cancelled));
        QCOMPARE(controller.shortcutLabel(2), QString("F9"));
        QVERIFY(!hotkeys.suspended());
        controller.beginCapture(2);
        QEvent focusOut(QEvent::FocusOut);
        QCoreApplication::sendEvent(&window, &focusOut);
        QCOMPARE(controller.capturingAction(), -1);
        QVERIFY(!hotkeys.suspended());
        HotkeyController restored(nullptr);
        restored.attach(nullptr, nullptr);
        QCOMPARE(restored.shortcutLabel(2), QString("F9"));
        controller.beginCapture(2);
        key(Qt::Key_Backspace, Qt::NoModifier, kVkBackspace);
        QCOMPARE(controller.toggleWindowStatus(), QString("unset"));
        controller.beginCapture(0);
        controller.beginCapture(1);
        controller.cancelCapture();
        QCOMPARE(controller.capturingAction(), -1);
        QVERIFY(!hotkeys.suspended());
        controller.restoreDefaults();
        QCOMPARE(controller.shortcutLabel(1), QString("Ctrl+Alt+P"));
        problem = statusProblem("ToggleRecord after restoreDefaults", HotkeyAction::ToggleRecord,
                                controller.toggleRecordStatus());
        QVERIFY2(problem.isEmpty(), problem.constData());
        problem = statusProblem("TogglePause after restoreDefaults", HotkeyAction::TogglePause,
                                controller.togglePauseStatus());
        QVERIFY2(problem.isEmpty(), problem.constData());
        setUiSettingsIniPath({});
    }

};

QTEST_MAIN(HotkeyControllerTests)
#include "hotkey_controller_tests.moc"

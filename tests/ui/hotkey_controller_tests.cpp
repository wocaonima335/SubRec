#include <QtTest>
#include "ui/backend/hotkey_controller.h"
#include "ui/backend/settings_store.h"
#include "shell/global_hotkeys.h"
#include <QQuickWindow>
#include <QTemporaryDir>
using namespace subrec::ui;

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
        QCOMPARE(controller.shortcutLabel(0), QString("Ctrl+Alt+R"));
        QCOMPARE(controller.toggleWindowStatus(), QString("unset"));
        QCOMPARE(controller.toggleRecordStatus(), QString("unsupported"));
        QVERIFY(controller.startupConflicts().isEmpty());
        QSignalSpy finished(&controller, &HotkeyController::captureFinished);
        auto key = [&](int code, Qt::KeyboardModifiers mods = Qt::NoModifier) {
            QKeyEvent event(QEvent::KeyPress, code, mods);
            QCoreApplication::sendEvent(&window, &event);
        };
        controller.beginCapture(2);
        QVERIFY(hotkeys.suspended());
        key(Qt::Key_R, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(finished.last().at(1).toInt(), int(HotkeyController::Duplicate));
        controller.beginCapture(2);
        key(Qt::Key_R);
        QCOMPARE(finished.last().at(1).toInt(), int(HotkeyController::Invalid));
        controller.beginCapture(2);
        key(Qt::Key_F9);
        QCOMPARE(finished.last().at(1).toInt(), int(HotkeyController::SavedWithNotice));
        QCOMPARE(controller.shortcutLabel(2), QString("F9"));
        QVERIFY(!hotkeys.suspended());
        controller.beginCapture(2);
        key(Qt::Key_Escape);
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
        key(Qt::Key_Backspace);
        QCOMPARE(controller.toggleWindowStatus(), QString("unset"));
        controller.beginCapture(0);
        controller.beginCapture(1);
        controller.cancelCapture();
        QCOMPARE(controller.capturingAction(), -1);
        QVERIFY(!hotkeys.suspended());
        controller.restoreDefaults();
        QCOMPARE(controller.shortcutLabel(1), QString("Ctrl+Alt+P"));
        setUiSettingsIniPath({});
    }

};

QTEST_MAIN(HotkeyControllerTests)
#include "hotkey_controller_tests.moc"

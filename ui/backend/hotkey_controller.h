#ifndef SUBREC_UI_HOTKEY_CONTROLLER_H
#define SUBREC_UI_HOTKEY_CONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include "shell/key_chord.h"
#include "shell/global_hotkeys.h"
#include <array>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
class QQuickWindow;
QT_END_NAMESPACE

namespace subrec::shell {
class GlobalHotkeys;
}

namespace subrec::ui {

// Batch 2, module H3. GUI thread. QML singleton `HotkeyController`: P3-5 page,
// KeySequenceField states (§2.5.12 / MINIMIZE_DESIGN §6.5) and shortcut texts in tooltips,
// tray menu and floating bar.
//
// Owns persistence: hotkey/toggleRecord ("Ctrl+Alt+R"), hotkey/togglePause ("Ctrl+Alt+P"),
// hotkey/toggleWindow ("") via openUiSettings(); storage format = shell::storageText().
// At attach() it loads the bindings and registers them with GlobalHotkeys; registration
// failures at startup are reported once through startupConflicts() (main.cpp sends N6).
//
// Capture: beginCapture() suspends all global hotkeys and installs an event filter on the
// main QQuickWindow; keys are read from QKeyEvent::nativeVirtualKey() (QML KeyEvent has no
// VK). Esc cancels, Backspace/Delete clears, focus loss cancels. On a complete chord:
// invalid -> Invalid; same as another action -> Duplicate; else saved, registered, and
// Saved / SavedWithNotice (lone F-key) / Conflict (kept but not live).
class HotkeyController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Display text "Ctrl + Alt + R"; "" = 未设置.
    Q_PROPERTY(QString toggleRecordText READ toggleRecordText NOTIFY bindingsChanged)
    Q_PROPERTY(QString togglePauseText READ togglePauseText NOTIFY bindingsChanged)
    Q_PROPERTY(QString toggleWindowText READ toggleWindowText NOTIFY bindingsChanged)
    // "ok" | "unset" | "conflict" | "unsupported" (non-Windows or no message window).
    Q_PROPERTY(QString toggleRecordStatus READ toggleRecordStatus NOTIFY bindingsChanged)
    Q_PROPERTY(QString togglePauseStatus READ togglePauseStatus NOTIFY bindingsChanged)
    Q_PROPERTY(QString toggleWindowStatus READ toggleWindowStatus NOTIFY bindingsChanged)
    // Action being captured, -1 when none.
    Q_PROPERTY(int capturingAction READ capturingAction NOTIFY captureChanged)
    // Live text while capturing: "" (show "请按下组合键…") or "Ctrl + Alt + …" (K-按住修饰键).
    Q_PROPERTY(QString captureText READ captureText NOTIFY captureChanged)

public:
    enum Action { ToggleRecord = 0, TogglePause = 1, ToggleWindow = 2 };
    Q_ENUM(Action)
    enum CaptureResult { Saved, SavedWithNotice, Conflict, Duplicate, Invalid, Cancelled, Cleared };
    Q_ENUM(CaptureResult)

    explicit HotkeyController(QObject *parent); // no default: see singleton_provider.h
    ~HotkeyController() override;

    static void setInstance(HotkeyController *instance);
    static HotkeyController *create(QQmlEngine *engine, QJSEngine *);

    // Borrowed; hotkeys may be null (AutoTest, unsupported) -> statuses "unsupported",
    // texts still editable and persisted. window: main QQuickWindow for the key filter.
    void attach(shell::GlobalHotkeys *hotkeys, QQuickWindow *window);
    // Display texts of actions whose registration failed at attach(); main.cpp turns a
    // non-empty list into N6 (window hidden, WindowController::onHotkeyConflicts) or a Toast.
    QStringList startupConflicts() const;

    QString toggleRecordText() const;
    QString togglePauseText() const;
    QString toggleWindowText() const;
    QString toggleRecordStatus() const;
    QString togglePauseStatus() const;
    QString toggleWindowStatus() const;
    int capturingAction() const;
    QString captureText() const;

    Q_INVOKABLE void beginCapture(int action);
    Q_INVOKABLE void cancelCapture();
    Q_INVOKABLE void clear(int action);
    Q_INVOKABLE void restoreDefaults(); // K1 defaults, re-register
    // Compact form for tooltips/menus ("Ctrl+Alt+R"), "" when unset.
    Q_INVOKABLE QString shortcutLabel(int action) const;

signals:
    void bindingsChanged();
    void captureChanged();
    // message: the K-* line under the field ("已被其他程序占用，请换一个组合",
    // "已用于'暂停/继续录制'", "需要带 Ctrl 或 Alt；不能使用 Win 键和 F12",
    // "其他程序将收不到 F9"); "" for Saved/Cancelled/Cleared.
    void captureFinished(int action, int result, const QString &message);

private:
    QPointer<shell::GlobalHotkeys> hotkeys_;
    std::array<shell::KeyChord,3> bindings_{};
    std::array<shell::RegisterResult,3> statuses_{};
    int capturing_ = -1;
    QString captureText_;
    bool eventFilter(QObject *, QEvent *) override;
    void finishCapture(int result, const QString &message = {});
    void saveBinding(int action, const shell::KeyChord &chord);
    QPointer<QQuickWindow> window_;
};

} // namespace subrec::ui

#endif // SUBREC_UI_HOTKEY_CONTROLLER_H

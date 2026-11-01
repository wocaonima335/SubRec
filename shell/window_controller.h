#ifndef SUBREC_SHELL_WINDOW_CONTROLLER_H
#define SUBREC_SHELL_WINDOW_CONTROLLER_H

#include <QObject>
#include <QString>
#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QQuickWindow;
QT_END_NAMESPACE

namespace subrec::shell {

class CaptureExclusion; // S0b: shell/capture_exclusion.h (implementation only)
class PowerRequest;     // S0b: shell/power_request.h (implementation only)
class ShellSettings;
class ShellStateSource;
class TrayController;
class WindowController;

// main.cpp supplies callbacks once, then moves this bundle into the controller.
// start must return captureScheduler().start()'s actual bool (false = rejected).
// Callbacks and their captures must remain valid for the controller's lifetime.
// There is no public callback accessor or callable start wrapper.
class RecordingActions {
public:
    RecordingActions(std::function<bool()> start,
                     std::function<void()> stop,
                     std::function<void()> pause,
                     std::function<void()> resume,
                     std::function<void()> openOutputFolder);
    RecordingActions(RecordingActions &&) noexcept = default;
    RecordingActions &operator=(RecordingActions &&) noexcept = default;
    RecordingActions(const RecordingActions &) = delete;
    RecordingActions &operator=(const RecordingActions &) = delete;

private:
    friend class WindowController;
    std::function<bool()> start;
    std::function<void()> stop;
    std::function<void()> pause;
    std::function<void()> resume;
    std::function<void()> openOutputFolder;
};

// GUI thread only. QML context name: shell. All borrowed dependencies must
// outlive this object; tray may be null. attachMainWindow does not take ownership.
//
// START INVARIANT (sections 4.4.5, 8.4.7): EVERY recording-start entry point must
// reach requestStart(). QML startRecording -> QmlBridge -> requestStart(); tray
// startStopRequested and future hotkeys -> requestStartStop() -> requestStart().
// Retry buttons and shared main.cpp startRecording/AutoTest helpers must also
// route here, never directly to the scheduler/recorder. S9 must wire these paths;
// a header cannot prevent external code from calling existing engine APIs.
// Only requestStart() may invoke actions_.start, synchronously AFTER
// exclusion.setCapturing(true). Rejection rolls back the tentative capturing
// flag; an accepted start retains exclusion through Stopping until Idle/Failed.
// Guard repeated/reentrant starts so rejection cannot clear an active session.
//
// AutoTest is NOT exempt from the start invariant. Per section 8.7 item 5 it
// skips single-instance/tray setup (and hotkeys, section 3.8), but closing still
// goes through this controller. Pass tray=nullptr: effective tray availability
// is false and requestClose takes the Exit path regardless of persisted C3.
// If scripts end runs by closing the window, treat C3 as Exit for that run,
// without persisting the override. Shutdown/logout uses aboutToQuit stopping,
// with no D1/D7 prompt.
class WindowController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool windowShown READ windowShown NOTIFY windowShownChanged)

public:
    WindowController(ShellSettings &settings, ShellStateSource &state,
                     TrayController *tray, CaptureExclusion &exclusion,
                     PowerRequest &power, RecordingActions actions,
                     QObject *parent = nullptr);
    ~WindowController() override;
    bool windowShown() const; // True only for W1, not minimized.
    // Observe visibility/geometry; disable quit-on-last-window-closed. Visibility
    // drives PreviewBridge::setPreviewWanted(true for W1, false for W2/W3).
    void attachMainWindow(QQuickWindow *window);
    void startHidden(); // C4/--minimized; ignored without an enabled, available tray.

    Q_INVOKABLE void requestClose(); // Titlebar/Alt+F4/onClosing (reject native close).
    Q_INVOKABLE void requestMinimize();
    Q_INVOKABLE void resolveCloseChoice(bool toTray, bool remember); // D7 result
    Q_INVOKABLE void confirmExit(); // D1 result
    Q_INVOKABLE void cancelExit();

public slots:
    void requestStart();
    void requestStop();
    // Explicit pause/resume preserve QmlBridge's separate pauseRecording and
    // readyRecording signals: wrong-state calls are no-ops, never toggles.
    void requestPause();
    void requestResume();
    void requestPauseResume(); // Running -> pause; Paused -> resume; otherwise no-op.
    // Idle/Failed MUST delegate to requestStart(), never invoke actions_.start.
    // Running/Paused delegate to requestStop(); other states are no-ops.
    void requestStartStop();
    void showMainWindow(); // Restore geometry/maximized state, raise, requestActivate.
    void hideToTray(); // Fall back to taskbar minimization without a usable tray.
    void toggleWindow();
    void beginExit(); // Show window before D1, or quit if no confirmation needed.
    void onSubtitleResult(bool ok, const QString &pathOrError); // N3, unconnected in batch 1.

signals:
    void closeChoiceRequested(bool capturing, const QString &timeText);
    void exitConfirmRequested(int variant, const QString &timeText); // 0..3 = D1a..D1d
    void windowShownChanged();
    void settingsPageRequested();

private:
    // Keep callbacks private to the controller, not in a public bridge/source.
    RecordingActions actions_;
    class Private;
    std::unique_ptr<Private> d;
};

// State coupling: Preparing activates PowerRequest and applies C7. Idle/Failed
// clears capturing/power; Stopping->Idle without error may notify Saved, Failed
// may notify CaptureFailed, and a deferred first-tray hint may then be delivered.
// Every snapshot refreshes the tray; notifications obey section 3.5 throughout.

} // namespace subrec::shell

#endif // SUBREC_SHELL_WINDOW_CONTROLLER_H

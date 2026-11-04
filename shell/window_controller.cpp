#include "shell/window_controller.h"
#include "shell/capture_exclusion.h"
#include "shell/power_request.h"
#include "shell/shell_logic.h"
#include "shell/shell_settings.h"
#include "shell/shell_state_source.h"
#include "shell/tray_controller.h"
#ifdef SUBREC_SHELL_CONTROLLER_TEST_DOUBLES
#include "fakes/preview_bridge_double.h"
#else
#include "opengl/ffglitem.h"
#endif

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPointer>
#include <QQuickWindow>
#include <QScopedValueRollback>
#include <QTimer>
#include <utility>

namespace subrec::shell {

RecordingActions::RecordingActions(std::function<bool()> start, std::function<void()> stop,
    std::function<void()> pause, std::function<void()> resume, std::function<void()> openOutputFolder)
    : start(std::move(start)), stop(std::move(stop)), pause(std::move(pause)),
      resume(std::move(resume)), openOutputFolder(std::move(openOutputFolder)) {}

class WindowController::Private {
public:
    Private(WindowController &owner, ShellSettings &settings, ShellStateSource &source,
            TrayController *tray, CaptureExclusion &exclusion, PowerRequest &power)
        : owner(owner), settings(settings), source(source), tray(tray), exclusion(exclusion), power(power) {}
    WindowController &owner;
    ShellSettings &settings;
    ShellStateSource &source;
    TrayController *tray;
    CaptureExclusion &exclusion;
    PowerRequest &power;
    QPointer<QQuickWindow> window;
    ShellSnapshot last;
    WindowVisibility visibility = WindowVisibility::Shown;
    QWindow::Visibility restoreVisibility = QWindow::Windowed;
    QRect normalGeometry;
    bool starting = false, startPending = false;
    bool closePending = false, exitPending = false, shuttingDown = false;
    bool hintPending = false, hintTimerActive = false;

    bool trayUsable() const { return tray && settings.trayEnabled() && TrayController::systemTrayAvailable(); }
    ShellSnapshot snapshot() const { auto s = source.snapshot(); s.window = visibility; return s; }
    void updateTray() { if (tray) tray->update(snapshot()); }

    bool notify(NotifyId id, const QString &title, const QString &body, QSystemTrayIcon::MessageIcon icon)
    {
        const auto s = snapshot();
        if (!trayUsable() || !QSystemTrayIcon::supportsMessages() || startPending
            || !notificationAllowed(s.window, s.state)) return false;
        tray->notify(id, title, body, icon);
        return true;
    }

    void deliverHint()
    {
        if (!hintPending || hintTimerActive || settings.trayHintShown()) return;
        if (notify(NotifyId::TrayHint, QStringLiteral("字录仍在后台运行"),
                QStringLiteral("点任务栏右下角的字录图标可以打开窗口。Windows 11 可能把图标收在“^”里，可以在 任务栏设置 › 其他系统托盘图标 中设为始终显示。"),
                QSystemTrayIcon::Information)) {
            hintPending = false;
            settings.setTrayHintShown(true);
        }
    }

    void rememberGeometry()
    {
        if (window && window->visibility() == QWindow::Windowed) normalGeometry = window->geometry();
    }

    void visibilityChanged(bool allowHint = true)
    {
        if (!window) return;
        const auto v = window->visibility();
        if (v == QWindow::Minimized && hideOnMinimize(trayUsable(), settings.minimizeToTray())) {
            window->hide();
            return;
        }
        const auto next = v == QWindow::Hidden ? WindowVisibility::HiddenToTray
            : v == QWindow::Minimized ? WindowVisibility::Minimized : WindowVisibility::Shown;
        if (next == WindowVisibility::Shown) {
            restoreVisibility = v;
            rememberGeometry();
        }
        const bool changed = next != visibility;
        visibility = next;
        PreviewBridge::instance().setPreviewWanted(visibility == WindowVisibility::Shown);
        if (changed) emit owner.windowShownChanged();
        if (allowHint && next == WindowVisibility::HiddenToTray && trayUsable() && !settings.trayHintShown()) hintPending = true;
        updateTray();
        deliverHint();
    }

    void applySettings()
    {
        exclusion.setMode(settings.excludeMode());
        if (tray) tray->setEnabled(settings.trayEnabled());
        // Disabling the only way back must never strand a hidden window.
        if (!trayUsable() && visibility == WindowVisibility::HiddenToTray) owner.showMainWindow();
        updateTray();
    }

    void refresh()
    {
        const auto s = snapshot();
        const auto previous = last;
        // A tentative start survives unrelated Idle/time/device signals emitted
        // inside start(). Once the engine publishes an active/terminal transition,
        // its state owns the lifetime of exclusion and power again.
        if (isCapturing(s.state) || (s.state == RecState::Failed && s.state != previous.state)) startPending = false;
        const bool active = isCapturing(s.state) || startPending;
        exclusion.setCapturing(active);
        power.setActive(active);
        last = s; // Publish before actions/signals that can reenter.
        if (s.state == RecState::Preparing && previous.state != RecState::Preparing && settings.hideOnRecordStart())
            owner.hideToTray();
        bool notified = false;
        if (previous.state == RecState::Stopping && (s.state == RecState::Idle || s.state == RecState::NoDevice)
            && s.captureError.isEmpty()) {
            notified = notify(NotifyId::Saved, QStringLiteral("录像已保存"),
                QStringLiteral("%1 · %2").arg(QFileInfo(previous.outputFile).fileName(), previous.timeText),
                QSystemTrayIcon::Information);
        } else if (s.state == RecState::Failed && previous.state != RecState::Failed) {
            notified = notify(NotifyId::CaptureFailed, QStringLiteral("录制失败"), s.captureError, QSystemTrayIcon::Critical);
        }
        updateTray();
        // Do not immediately replace N2/N4 with the deferred N1 balloon.
        if (notified && hintPending && !hintTimerActive) {
            hintTimerActive = true;
            QTimer::singleShot(10000, &owner, [this] { hintTimerActive = false; deliverHint(); });
        } else {
            deliverHint();
        }
    }
};

WindowController::WindowController(ShellSettings &settings, ShellStateSource &state,
    TrayController *tray, CaptureExclusion &exclusion, PowerRequest &power,
    RecordingActions actions, QObject *parent)
    : QObject(parent), actions_(std::move(actions)),
      d(std::make_unique<Private>(*this, settings, state, tray, exclusion, power))
{
    connect(&settings, &ShellSettings::changed, this, [this] { d->applySettings(); });
    connect(&state, &ShellStateSource::changed, this, [this] { d->refresh(); });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { d->shuttingDown = true; });
#if QT_CONFIG(sessionmanager)
    connect(qApp, &QGuiApplication::commitDataRequest, this, [this] { d->shuttingDown = true; });
#endif
    if (tray) {
        exclusion.addMenu(tray->menu());
        connect(tray, &TrayController::toggleWindowRequested, this, &WindowController::toggleWindow);
        connect(tray, &TrayController::startStopRequested, this, &WindowController::requestStartStop);
        connect(tray, &TrayController::pauseResumeRequested, this, &WindowController::requestPauseResume);
        connect(tray, &TrayController::exitRequested, this, &WindowController::beginExit);
        connect(tray, &TrayController::openOutputFolderRequested, this, [this] {
            if (actions_.openOutputFolder) actions_.openOutputFolder();
        });
        connect(tray, &TrayController::openSettingsRequested, this, [this] {
            showMainWindow(); emit settingsPageRequested();
        });
        connect(tray, &TrayController::notificationClicked, this, [this](NotifyId) { showMainWindow(); });
    }
    d->applySettings();
    d->refresh();
}

WindowController::~WindowController() = default;
bool WindowController::windowShown() const { return d->visibility == WindowVisibility::Shown; }

void WindowController::attachMainWindow(QQuickWindow *window)
{
    if (d->window) disconnect(d->window, nullptr, this, nullptr);
    d->window = window;
    QGuiApplication::setQuitOnLastWindowClosed(false);
    if (!window) {
        d->visibility = WindowVisibility::HiddenToTray;
        PreviewBridge::instance().setPreviewWanted(false);
        emit windowShownChanged();
        return;
    }
    d->exclusion.addWindow(window);
    connect(window, &QWindow::visibilityChanged, this, [this] { d->visibilityChanged(); });
    const auto geometryChanged = [this] { d->rememberGeometry(); };
    connect(window, &QWindow::xChanged, this, geometryChanged);
    connect(window, &QWindow::yChanged, this, geometryChanged);
    connect(window, &QWindow::widthChanged, this, geometryChanged);
    connect(window, &QWindow::heightChanged, this, geometryChanged);
    connect(window, &QObject::destroyed, this, [this] {
        d->visibility = WindowVisibility::HiddenToTray;
        PreviewBridge::instance().setPreviewWanted(false);
        emit windowShownChanged();
    });
    // A newly constructed QML window is initially hidden, not a user tray entry.
    d->visibilityChanged(false);
}

void WindowController::startHidden() { if (d->trayUsable()) hideToTray(); }

void WindowController::requestClose()
{
    if (d->shuttingDown) { QCoreApplication::quit(); return; }
    if (d->closePending || d->exitPending) return;
    switch (decideClose(d->trayUsable(), d->settings.closeAction())) {
    case CloseOutcome::AskUser: {
        d->closePending = true;
        showMainWindow();
        const auto s = d->snapshot();
        emit closeChoiceRequested(isCapturing(s.state), s.timeText);
        break;
    }
    case CloseOutcome::HideToTray: hideToTray(); break;
    case CloseOutcome::Exit: beginExit(); break;
    }
}

void WindowController::requestMinimize()
{
    if (hideOnMinimize(d->trayUsable(), d->settings.minimizeToTray())) hideToTray();
    else if (d->window) { d->rememberGeometry(); d->window->showMinimized(); }
}

void WindowController::resolveCloseChoice(bool toTray, bool remember)
{
    d->closePending = false;
    if (remember) d->settings.setCloseActionText(toTray ? QStringLiteral("tray") : QStringLiteral("exit"));
    if (toTray) hideToTray(); else beginExit();
}

void WindowController::confirmExit()
{
    if (!d->exitPending) return;
    d->exitPending = false;
    d->shuttingDown = true;
    // main.cpp's existing aboutToQuit hook drains/stops the scheduler.
    QCoreApplication::quit();
}
void WindowController::cancelExit() { d->exitPending = false; d->closePending = false; }

void WindowController::requestStart()
{
    const auto state = d->snapshot().state;
    if (d->starting || d->startPending || d->shuttingDown
        || isCapturing(state) || !actions_.start) return;
    // NoDevice is NOT rejected here: the engine reports the error and its fix
    // suggestion (Failed + captureError), as before the shell (review M1).
    QScopedValueRollback<bool> guard(d->starting, true);
    d->startPending = true;
    d->exclusion.setCapturing(true); // MUST precede the first synchronous frame grab.
    d->power.setActive(true);
    const bool accepted = actions_.start();
    if (!accepted) d->startPending = false;
    d->refresh(); // Rollback on rejection; never clear an already active session.
}

void WindowController::requestStop()
{
    const auto state = d->snapshot().state;
    if ((state == RecState::Running || state == RecState::Paused) && actions_.stop) actions_.stop();
}
void WindowController::requestPause()
{
    if (d->snapshot().state == RecState::Running && actions_.pause) actions_.pause();
}
void WindowController::requestResume()
{
    if (d->snapshot().state == RecState::Paused && actions_.resume) actions_.resume();
}
void WindowController::requestPauseResume()
{
    if (d->snapshot().state == RecState::Paused) requestResume(); else requestPause();
}
void WindowController::requestStartStop()
{
    const auto state = d->snapshot().state;
    if (state == RecState::Idle || state == RecState::Failed) requestStart();
    else requestStop();
}

void WindowController::showMainWindow()
{
    if (!d->window) return;
    if (!windowShown()) {
        const auto restore = d->restoreVisibility;
        const auto geometry = d->normalGeometry;
        if (geometry.isValid()) d->window->setGeometry(geometry);
        if (restore == QWindow::Maximized) d->window->showMaximized();
        else if (restore == QWindow::FullScreen) d->window->showFullScreen();
        else d->window->showNormal();
    }
    d->window->raise();
    d->window->requestActivate();
}

void WindowController::hideToTray()
{
    if (!d->window) return;
    d->rememberGeometry();
    if (d->trayUsable()) {
        d->window->hide();
        d->visibilityChanged(); // Also handles --minimized before the first show().
    } else d->window->showMinimized();
}
void WindowController::toggleWindow() { if (windowShown()) hideToTray(); else showMainWindow(); }

void WindowController::beginExit()
{
    if (d->exitPending) return;
    d->closePending = false;
    const auto s = d->snapshot();
    if (!d->shuttingDown && exitNeedsConfirm(s.state, s.subtitleFinishing, d->settings.confirmExit())) {
        d->exitPending = true;
        showMainWindow();
        emit exitConfirmRequested(exitConfirmVariant(s.state, s.subtitleFinishing), s.timeText);
    } else {
        d->shuttingDown = true;
        QCoreApplication::quit();
    }
}

void WindowController::onSubtitleResult(bool ok, const QString &pathOrError)
{
    d->notify(NotifyId::SubtitleResult, ok ? QStringLiteral("字幕已生成") : QStringLiteral("字幕生成失败"),
        ok ? QFileInfo(pathOrError).fileName() : pathOrError,
        ok ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning);
}

// TODO(H4): N6 text per MINIMIZE_DESIGN §3.5, click -> pageRequested("settings/hotkeys").
void WindowController::onHotkeyConflicts(const QStringList &) {}

} // namespace subrec::shell

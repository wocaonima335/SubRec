#ifndef SUBREC_UI_SHELL_VIEW_H
#define SUBREC_UI_SHELL_VIEW_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace subrec::shell {
class ShellSettings;
class WindowController;
} // namespace subrec::shell

namespace subrec::ui {

// Module B2. GUI thread. QML singleton `ShellView`: new-UI access to the batch-1 shell
// (shell/window_controller.h, shell/shell_settings.h). Replaces the `shell` and
// `shellSettings` context properties for SubRec.Ui files; the old UI keeps using those.
//
// Pure forwarding: no decisions are made here. All close/minimize/exit logic and the
// START INVARIANT stay in WindowController; every start in the new UI (record button,
// S6 retry, F9, floating bar) calls requestStart()/requestStartStop() below.
// Unattached: settings return their §5.1 defaults, requests are no-ops.
class ShellView : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // WindowController::windowShown (W1 only).
    Q_PROPERTY(bool windowShown READ windowShown NOTIFY windowShownChanged)
    // QSystemTrayIcon::isSystemTrayAvailable(), injected at attach. False: P3-1 C1–C4 disabled.
    Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT)

    // ShellSettings, read/write, same names, keys and defaults (MINIMIZE_DESIGN §5.1).
    Q_PROPERTY(bool trayEnabled READ trayEnabled WRITE setTrayEnabled NOTIFY settingsChanged)                 // C1
    Q_PROPERTY(bool minimizeToTray READ minimizeToTray WRITE setMinimizeToTray NOTIFY settingsChanged)        // C2
    Q_PROPERTY(QString closeAction READ closeAction WRITE setCloseAction NOTIFY settingsChanged)              // C3 ask|tray|exit
    Q_PROPERTY(bool startMinimized READ startMinimized WRITE setStartMinimized NOTIFY settingsChanged)        // C4
    Q_PROPERTY(QString floatingBar READ floatingBar WRITE setFloatingBar NOTIFY settingsChanged)              // C5 whenHidden|always|never (batch 2)
    Q_PROPERTY(QString excludeFromCapture READ excludeFromCapture WRITE setExcludeFromCapture NOTIFY settingsChanged) // C6 recording|always|never
    Q_PROPERTY(bool hideOnRecordStart READ hideOnRecordStart WRITE setHideOnRecordStart NOTIFY settingsChanged) // C7
    Q_PROPERTY(bool confirmExitWhileRecording READ confirmExitWhileRecording WRITE setConfirmExitWhileRecording NOTIFY settingsChanged)
    Q_PROPERTY(bool excludeSupported READ excludeSupported CONSTANT)                                          // build >= 19041

public:
    explicit ShellView(QObject *parent); // no default: see singleton_provider.h
    ~ShellView() override;

    static void setInstance(ShellView *instance);
    static ShellView *create(QQmlEngine *engine, QJSEngine *);

    // GUI thread, before QML loads. Both borrowed and must outlive the QML engine.
    void attach(shell::WindowController *controller, shell::ShellSettings *settings,
                bool trayAvailable);

    bool windowShown() const;
    bool trayAvailable() const;
    bool trayEnabled() const;
    void setTrayEnabled(bool enabled);
    bool minimizeToTray() const;
    void setMinimizeToTray(bool enabled);
    QString closeAction() const;
    void setCloseAction(const QString &action);
    bool startMinimized() const;
    void setStartMinimized(bool enabled);
    QString floatingBar() const;
    void setFloatingBar(const QString &mode);
    QString excludeFromCapture() const;
    void setExcludeFromCapture(const QString &mode);
    bool hideOnRecordStart() const;
    void setHideOnRecordStart(bool enabled);
    bool confirmExitWhileRecording() const;
    void setConfirmExitWhileRecording(bool enabled);
    bool excludeSupported() const;

    // ---- forwarded to WindowController (same semantics) ----
    Q_INVOKABLE void requestClose();     // title-bar close, AppWindow.onClosing (reject native close)
    Q_INVOKABLE void requestMinimize();  // title-bar minimize
    Q_INVOKABLE void resolveCloseChoice(bool toTray, bool remember); // D7 OK
    Q_INVOKABLE void confirmExit();      // D1 "停止并退出" / "仍然退出"
    Q_INVOKABLE void cancelExit();       // D1/D7 cancel, Esc, close icon
    Q_INVOKABLE void requestStart();     // record button (idle/failed), S6 "重试"
    Q_INVOKABLE void requestStop();
    Q_INVOKABLE void requestPause();
    Q_INVOKABLE void requestResume();
    Q_INVOKABLE void requestStartStop(); // F9
    Q_INVOKABLE void requestPauseResume(); // F10
    Q_INVOKABLE void showMainWindow();

signals:
    void windowShownChanged();
    void settingsChanged();
    // Re-emitted from WindowController.
    void closeChoiceRequested(bool capturing, const QString &timeText);   // open D7 / D7-R
    void exitConfirmRequested(int variant, const QString &timeText);      // open D1a..D1d (0..3)
    void settingsPageRequested();                                         // tray "设置…" -> P3-1
    // Batch 2 (WindowController additions, module H4):
    // A start request was refused (no device, engine rejected). Window visible -> Toast;
    // hidden -> WindowController already sent N5. reason is user-facing Chinese text.
    void startRejected(const QString &reason);
    // Notification click / tray: "record" | "library" | "settings/general" |
    // "settings/subtitle" | "settings/hotkeys". AppWindow switches page + sub-page.
    void pageRequested(const QString &page);

private:
    QPointer<shell::WindowController> controller_;
    QPointer<shell::ShellSettings> settings_;
    bool trayAvailable_ = false;
};

} // namespace subrec::ui

#endif // SUBREC_UI_SHELL_VIEW_H

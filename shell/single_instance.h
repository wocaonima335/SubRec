#pragma once

// S0b contract (ZCode). Implementation: shell/single_instance.cpp (S4).
// Design: MINIMIZE_DESIGN.md §3.8, §4.6, §8.4.6.

#include <QObject>
#include <QString>


namespace subrec::shell {

// One SubRec process per login session.
//
// Protocol: named mutex "Local\SubRec.SingleInstance" decides the role. The primary
// listens on a QLocalServer whose name contains the current user name. A secondary
// calls AllowSetForegroundWindow(ASFW_ANY), connects, sends one line ("show" or
// "noop"), and waits for an "ok" reply.
//
// Threading: GUI thread only. acquire() must run at the very start of main(), after the
//   QApplication is constructed (QLocalSocket needs an event dispatcher) and before any
//   window, device or recorder is created. main.cpp skips this whole class in AutoTest
//   mode (§3.8).
// Lifetime: the primary keeps the object (and thus the mutex handle and server) alive
//   until the process exits; the destructor closes both. A secondary may destroy it
//   right after notifyPrimary() and then exit(0).
class SingleInstance : public QObject
{
    Q_OBJECT
public:
    enum class Role { Primary, Secondary };
    enum class Request { Show, Noop };   // Noop is sent when started with --minimized

    explicit SingleInstance(QObject *parent = nullptr);
    ~SingleInstance() override;

    // Creates/opens the mutex. ERROR_ALREADY_EXISTS => Secondary.
    // If CreateMutex itself fails, returns Primary (fail open: better a rare duplicate
    // than refusing to start) and logs "[Shell]". Call once.
    Role acquire();

    // Primary only. Starts the local server (removing a stale server name first) on a
    // dedicated thread, so a secondary is acknowledged even while the primary's GUI
    // thread is still busy starting up (fix for #5); showRequested is queued to the
    // GUI thread and delivered once its event loop runs.
    // Returns false if listening failed; the app still runs, but a later second launch
    // will time out and show its "not responding" message box.
    bool listen();

    // Secondary only. Blocking, at most timeoutMs in total (connect + write + reply).
    // Returns true when the primary acknowledged. On false, the caller (main.cpp) shows
    // the system message box from §4.6 item 3 and exits; this method shows no UI.
    bool notifyPrimary(Request request, int timeoutMs = 1000);

    // Same name the server uses; exposed for tests.
    static QString serverName();

signals:
    // Primary: emitted on the GUI thread after a "show" request was acknowledged.
    // Connected by main.cpp to WindowController::showMainWindow(). "noop" emits nothing.
    void showRequested();

private:
    struct Worker;                    // server thread; answers without the GUI event loop
    void *m_mutex = nullptr;          // HANDLE; void* keeps <windows.h> out of the header
    Worker *m_worker = nullptr;
};

} // namespace subrec::shell

// TB1 unit tests, non-Windows rules (UI_BATCH_PLAN.md §2.3): everything is a no-op.
// supported() is false, attachWindow/setKind/setDescription store nothing observable,
// detaching and re-attaching do not crash, and the tray-icon mapping itself is covered by
// the shared shell_logic tests. The COM half (ITaskbarList3::SetOverlayIcon, HWND
// recreation, Explorer restart) is MinGW -fsyntax-only checked here and PC3-verified.
// An injectable acquire hook covers the no-cache and failed-HRESULT paths on every CI OS.
#include "shell/taskbar_overlay.h"

#include <QGuiApplication>
#include <QWindow>
#include <cstdio>

using namespace subrec::shell;

namespace {

int g_failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

struct SessionCounters {
    int acquire = 0;
    int setOverlay = 0;
    int release = 0;
    int hr = 0;
};

SessionCounters g_session;

TaskbarOverlay::Session testAcquire()
{
    ++g_session.acquire;
    TaskbarOverlay::Session session;
    session.handle = &g_session;
    session.setOverlayIcon = [](void *handle, quintptr, void *, const void *) -> int {
        auto *counters = static_cast<SessionCounters *>(handle);
        ++counters->setOverlay;
        return counters->hr;
    };
    session.release = [](void *handle) {
        ++static_cast<SessionCounters *>(handle)->release;
    };
    return session;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv); // QWindow needs a GUI application instance

#ifndef Q_OS_WIN
    check(!TaskbarOverlay::supported(), "supported() is false off Windows");
#else
    check(TaskbarOverlay::supported(), "supported() is true on Windows");
#endif

    {
        TaskbarOverlay overlay;
        QWindow window; // no native surface needed: off Windows attach only stores it

        overlay.setKind(TrayIconKind::Recording);
        overlay.setDescription(QStringLiteral("录制中"));
        overlay.attachWindow(&window);

        // All five states + Idle clear, with re-attach in between; none of this may
        // crash or assert regardless of platform.
        for (const TrayIconKind kind : {TrayIconKind::Idle, TrayIconKind::Transition,
                                        TrayIconKind::Recording, TrayIconKind::Paused,
                                        TrayIconKind::Error}) {
            overlay.setKind(kind);
            check(true, "setKind accepts the state");
        }
        overlay.setDescription(QString());
        overlay.attachWindow(nullptr);
        overlay.attachWindow(&window);
        overlay.setKind(TrayIconKind::Recording);
    }
    check(true, "destroy with an attached window is clean");

    // Injected factory: every apply must acquire and release; a failed SetOverlayIcon
    // must still release and a later apply must still run (would fail if the pointer
    // were cached or a failure path skipped release).
    {
        struct ResetHook {
            ~ResetHook() { TaskbarOverlay::setTestAcquire(nullptr); }
        } resetHook;
        g_session = SessionCounters{};
        TaskbarOverlay::setTestAcquire(&testAcquire);

        TaskbarOverlay overlay;
        QWindow window;
        overlay.attachWindow(&window);
        check(g_session.acquire == 1, "attach acquires a session");
        check(g_session.setOverlay == 1, "attach calls SetOverlayIcon");
        check(g_session.release == 1, "attach releases the session (no cache)");

        overlay.setKind(TrayIconKind::Recording);
        check(g_session.acquire == 2, "setKind acquires a new session");
        check(g_session.setOverlay == 2, "setKind calls SetOverlayIcon on the new session");
        check(g_session.release == 2, "setKind releases after apply (no cache)");

        g_session.hr = static_cast<int>(0x80004005); // E_FAIL
        overlay.setKind(TrayIconKind::Idle);
        check(g_session.acquire == 3, "failed apply still acquires");
        check(g_session.setOverlay == 3, "failed apply still calls SetOverlayIcon");
        check(g_session.release == 3, "failed SetOverlayIcon still releases");

        g_session.hr = 0;
        overlay.setKind(TrayIconKind::Recording);
        check(g_session.acquire == 4, "apply after a failure acquires again");
        check(g_session.setOverlay == 4, "apply after a failure calls SetOverlayIcon again");
        check(g_session.release == 4, "apply after a failure still releases");
    }

    if (g_failures == 0) std::printf("taskbar_overlay_tests: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}

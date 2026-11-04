// TB1 unit tests, non-Windows rules (UI_BATCH_PLAN.md §2.3): everything is a no-op.
// supported() is false, attachWindow/setKind/setDescription store nothing observable,
// detaching and re-attaching do not crash, and the tray-icon mapping itself is covered by
// the shared shell_logic tests. The COM half (ITaskbarList3::SetOverlayIcon, HWND
// recreation, Explorer restart) is MinGW -fsyntax-only checked here and PC3-verified.
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

    if (g_failures == 0) std::printf("taskbar_overlay_tests: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}

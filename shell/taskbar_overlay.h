#ifndef SUBREC_SHELL_TASKBAR_OVERLAY_H
#define SUBREC_SHELL_TASKBAR_OVERLAY_H

#include "shell/shell_types.h"

#include <QObject>

#include <memory>

QT_BEGIN_NAMESPACE
class QWindow;
QT_END_NAMESPACE

namespace subrec::shell {

// Batch 2, module TB1 (X7). GUI thread. Small status icon on the main window's taskbar
// button via COM ITaskbarList3::SetOverlayIcon (Qt 6 has no QtWinExtras).
// Kind mapping: Recording -> red dot, Paused -> amber pause, Transition -> blue dot,
// Error -> red "!", Idle -> overlay cleared. Icons come from tray_icon_factory (16 px).
// Only meaningful while the window has a taskbar button (W1/W2); harmless otherwise.
// ITaskbarList3 is acquired for a single apply and released before apply returns (no
// long-lived cache). A failed HRESULT from SetOverlayIcon (or acquire/HrInit) releases
// and skips rather than crashing. Re-applies after HWND recreation, Explorer restart,
// WM_DPICHANGED and WM_DISPLAYCHANGE. Non-Windows: no-op unless a test acquire is set.
class TaskbarOverlay : public QObject {
    Q_OBJECT

public:
    explicit TaskbarOverlay(QObject *parent = nullptr);
    ~TaskbarOverlay() override;

    static bool supported();
    void attachWindow(QWindow *window); // borrowed; nullptr detaches
    void setKind(TrayIconKind kind);    // same mapping as the tray icon (trayIconFor)
    // Accessibility text passed to SetOverlayIcon, e.g. "录制中".
    void setDescription(const QString &text);

    // Test-only (CI), per instance. Production never calls this. Stored on the same
    // Private that apply() reads so the hook cannot miss apply() across linkage
    // units (Windows CI at 928d794 left all 12 hook counters at 0). nullptr
    // restores production acquire: CoCreateInstance + HrInit per apply on Windows,
    // none otherwise.
    struct Session {
        void *handle = nullptr;
        // 0 = S_OK. Non-zero is a failed HRESULT: the apply releases and skips.
        int (*setOverlayIcon)(void *handle, quintptr hwnd, void *icon, const void *text) = nullptr;
        void (*release)(void *handle) = nullptr;
    };
    using AcquireFn = Session (*)();
    void setTestAcquire(AcquireFn fn);

protected:
    // Watches the attached window: HWND recreation (PlatformSurface / WinIdChange)
    // re-applies the overlay to the new taskbar button.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reapply(); // store-then-apply entry point; no-op off Windows without a test hook

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace subrec::shell

#endif // SUBREC_SHELL_TASKBAR_OVERLAY_H

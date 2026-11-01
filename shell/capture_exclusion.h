#pragma once

// S0b contract (ZCode). Implementation: shell/capture_exclusion.cpp (S5).
// Design: MINIMIZE_DESIGN.md §3.7, §4.4, §8.4.6.

#include "shell/shell_types.h" // ExcludeMode (S0a, Codex)

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QMenu;
class QWindow;

namespace subrec::shell {

// Keeps this process's top-level windows out of screen capture by setting
// SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE) while shouldExclude(mode,
// capturing, supported()) is true, and WDA_NONE otherwise.
//
// Threading: GUI thread only (create, call, destroy). Not thread-safe.
// Lifetime: must outlive nothing in particular; registered windows/menus are held as
//   QPointer, so destroying a window first is fine. The destructor does NOT reset
//   affinity (the process is exiting; HWNDs die with it).
// Platform: on non-Windows builds and on Windows < 10.0.19041, supported() is false
//   and every method is a no-op that never calls SetWindowDisplayAffinity (on older
//   builds 0x11 behaves like WDA_MONITOR and turns the window black, §4.4 item 2).
// Failure semantics: Win32 failures never throw and never block recording. Each failed
//   set or read-back mismatch is logged with the "[Shell]" tag and emits applyFailed();
//   effective() keeps reporting the intended state, not the read-back result.
class CaptureExclusion : public QObject
{
    Q_OBJECT
public:
    // Windows 10 build >= 19041, detected via RtlGetVersion (not manifest-dependent).
    // Computed once and cached; safe to call before QApplication exists.
    static bool supported();

    explicit CaptureExclusion(QObject *parent = nullptr);
    ~CaptureExclusion() override;

    // From ShellSettings::excludeMode(). Re-applies to all registered windows immediately.
    void setMode(ExcludeMode mode);

    // Turning on MUST be called synchronously BEFORE captureScheduler().start(), because
    // ddagrab grabs its first frame during avfilter_graph_config (§4.4 item 5).
    // Turning off is called only after captureState returns to Idle or Failed, never
    // during Stopping. WindowController owns both call sites. Idempotent.
    void setCapturing(bool capturing);

    // Registers a top-level window. Applies the current state now if the HWND exists,
    // and installs an event filter that re-applies on QEvent::PlatformSurface
    // (SurfaceCreated), since Qt may recreate the native window. Null or duplicate
    // pointers are ignored.
    void addWindow(QWindow *window);

    // Registers a menu (tray menu). Re-applies on every aboutToShow, after winId().
    void addMenu(QMenu *menu);

    // Whether exclusion is currently intended to be applied (shouldExclude result).
    bool effective() const;

signals:
    // Diagnostic only; also written to the log with "[Shell]". Never shown to the user.
    void applyFailed(const QString &what);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void applyAll();

    ExcludeMode m_mode = ExcludeMode::RecordingOnly;
    bool m_capturing = false;
    QList<QPointer<QWindow>> m_windows;
    QList<QPointer<QMenu>> m_menus;
};

} // namespace subrec::shell

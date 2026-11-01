#include "shell/capture_exclusion.h"
#include "shell/shell_logic.h"

#include <QDebug>
#include <QEvent>
#include <QMenu>
#include <QPlatformSurfaceEvent>
#include <QWindow>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011 // Windows 10 2004 (build 19041)
#endif
#endif

namespace subrec::shell {

namespace {

#ifdef _WIN32
bool queryWindowsSupportsExclusion()
{
    using RtlGetVersionFn = LONG(WINAPI *)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    auto fn = reinterpret_cast<RtlGetVersionFn>(
        reinterpret_cast<void *>(GetProcAddress(ntdll, "RtlGetVersion")));
    if (!fn) return false;
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (fn(&info) != 0) return false;
    const bool ok = info.dwMajorVersion > 10
                    || (info.dwMajorVersion == 10 && info.dwBuildNumber >= 19041);
    qInfo().noquote() << QStringLiteral("[Shell] Windows %1.%2 build %3: capture exclusion %4")
                             .arg(info.dwMajorVersion).arg(info.dwMinorVersion)
                             .arg(info.dwBuildNumber)
                             .arg(ok ? QStringLiteral("supported") : QStringLiteral("unsupported"));
    return ok;
}

// Returns an empty string on success, otherwise a diagnostic.
QString applyAffinity(HWND hwnd, bool exclude)
{
    if (!hwnd) return QStringLiteral("no HWND");
    const DWORD want = exclude ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
    if (!SetWindowDisplayAffinity(hwnd, want))
        return QStringLiteral("SetWindowDisplayAffinity(0x%1) error=%2")
            .arg(want, 0, 16).arg(static_cast<qulonglong>(GetLastError()));
    DWORD got = 0xFFFFFFFF;
    if (!GetWindowDisplayAffinity(hwnd, &got))
        return QStringLiteral("GetWindowDisplayAffinity error=%1")
            .arg(static_cast<qulonglong>(GetLastError()));
    if (got != want)
        return QStringLiteral("read-back 0x%1, wanted 0x%2").arg(got, 0, 16).arg(want, 0, 16);
    return {};
}
#endif

} // namespace

bool CaptureExclusion::supported()
{
#ifdef _WIN32
    static const bool value = queryWindowsSupportsExclusion();
    return value;
#else
    return false;
#endif
}

CaptureExclusion::CaptureExclusion(QObject *parent)
    : QObject(parent)
{
}

CaptureExclusion::~CaptureExclusion() = default;

bool CaptureExclusion::effective() const
{
    return shouldExclude(m_mode, m_capturing, supported());
}

void CaptureExclusion::setMode(ExcludeMode mode)
{
    if (mode == m_mode) return;
    const bool before = effective();
    m_mode = mode;
    if (effective() != before) applyAll();
}

void CaptureExclusion::setCapturing(bool capturing)
{
    if (capturing == m_capturing) return;
    const bool before = effective();
    m_capturing = capturing;
    if (effective() != before) applyAll();
}

void CaptureExclusion::addWindow(QWindow *window)
{
    if (!window) return;
    for (const auto &w : m_windows)
        if (w == window) return;
    m_windows.append(QPointer<QWindow>(window));
    window->installEventFilter(this);
    // Only touch windows that already have a native handle; otherwise SurfaceCreated
    // will trigger the first apply. Never force winId() here (it would create the HWND
    // early with possibly wrong attributes).
    if (window->handle() && supported()) applyWindow(window);
}

void CaptureExclusion::addMenu(QMenu *menu)
{
    if (!menu) return;
    for (const auto &m : m_menus)
        if (m == menu) return;
    m_menus.append(QPointer<QMenu>(menu));
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        if (!supported()) return;
        menu->winId(); // ensure the popup's HWND exists before it is shown
        if (QWindow *handle = menu->windowHandle()) applyWindow(handle);
    });
}

bool CaptureExclusion::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::PlatformSurface) {
        auto *surfaceEvent = static_cast<QPlatformSurfaceEvent *>(event);
        if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
            if (auto *window = qobject_cast<QWindow *>(watched); window && supported()) {
                // Sent after the native surface (HWND) exists and before it is shown,
                // so apply synchronously to avoid a first unexcluded frame.
                applyWindow(window);
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void CaptureExclusion::applyAll()
{
    if (!supported()) return; // never call the API on unsupported systems
    const bool exclude = effective();
    qInfo().noquote() << QStringLiteral("[Shell] capture exclusion %1")
                             .arg(exclude ? QStringLiteral("on") : QStringLiteral("off"));
    for (const auto &w : m_windows)
        if (w && w->handle()) applyWindow(w);
    // Menus are re-applied on their next aboutToShow; a visible one is refreshed now.
    for (const auto &m : m_menus)
        if (m && m->isVisible())
            if (QWindow *handle = m->windowHandle()) applyWindow(handle);
}

void CaptureExclusion::applyWindow(QWindow *window)
{
#ifdef _WIN32
    if (!supported() || !window) return;
    const QString err = applyAffinity(reinterpret_cast<HWND>(window->winId()), effective());
    if (!err.isEmpty()) {
        const QString what = QStringLiteral("%1: %2").arg(window->objectName(), err);
        qWarning().noquote() << QStringLiteral("[Shell] capture exclusion failed on %1").arg(what);
        emit applyFailed(what);
    }
#else
    Q_UNUSED(window);
#endif
}

} // namespace subrec::shell

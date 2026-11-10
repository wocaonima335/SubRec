#include "shell/taskbar_overlay.h"

#include "shell/tray_icon_factory.h"

#include <QEvent>
#include <QPixmap>
#include <QWindow>

#include <vector>

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>

#include <objbase.h>
#include <shobjidl.h>
#include <windows.h>
#endif

#ifdef Q_OS_WIN
// QPixmap::toHICON() is QtGui-private; build the HICON from straight-alpha ARGB32 rows
// (mirrored: CreateBitmap reads bottom-up) via CreateIconIndirect.
HICON pixmapToHicon(const QPixmap &pixmap)
{
    if (pixmap.isNull())
        return nullptr;
    const QImage image =
        pixmap.toImage().convertToFormat(QImage::Format_ARGB32).mirrored(false, true);
    const int w = image.width();
    const int h = image.height();
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = CreateBitmap(w, h, 1, 32, image.constBits());
    std::vector<uchar> mask(static_cast<size_t>((w + 7) / 8) * static_cast<size_t>(h), 0);
    info.hbmMask = CreateBitmap(w, h, 1, 1, mask.data());
    const HICON icon = CreateIconIndirect(&info);
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    return icon;
}
#endif

namespace subrec::shell {

namespace {

#ifdef Q_OS_WIN
TaskbarOverlay::Session acquireProduction()
{
    ITaskbarList3 *taskbar = nullptr;
    if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&taskbar)))
        || !taskbar)
        return {};
    // ITaskbarList requires HrInit before any other method is called.
    if (FAILED(taskbar->HrInit())) {
        taskbar->Release();
        return {};
    }
    TaskbarOverlay::Session session;
    session.handle = taskbar;
    session.setOverlayIcon = [](void *handle, quintptr hwnd, void *icon, const void *text) -> int {
        auto *list = static_cast<ITaskbarList3 *>(handle);
        return static_cast<int>(list->SetOverlayIcon(reinterpret_cast<HWND>(hwnd),
                                                     static_cast<HICON>(icon),
                                                     static_cast<PCWSTR>(text)));
    };
    session.release = [](void *handle) {
        if (handle)
            static_cast<ITaskbarList3 *>(handle)->Release();
    };
    return session;
}
#endif

} // namespace

// TB1 (X7). Off Windows everything is a no-op storing nothing, unless a test acquire is
// injected. The Windows half is compile-checked with MinGW -fsyntax-only and exercised on
// PC3. ITaskbarList3 is never cached across applies (a stale vtable at SetOverlayIcon
// crashed 47056c1 after stop/Idle following a display-scale change).
class TaskbarOverlay::Private {
public:
    QWindow *window = nullptr;
    TrayIconKind kind = TrayIconKind::Idle;
    QString description;
    // Test hook, if set. Instance state (not a file-static) so apply() always
    // sees the same pointer the test stored on this object.
    TaskbarOverlay::AcquireFn testAcquire = nullptr;

#ifdef Q_OS_WIN
    UINT taskbarCreatedMessage = 0;
    bool haveHwnd = false;

    // Explorer restarts re-create the taskbar button and broadcast TaskbarButtonCreated;
    // that message never becomes a QEvent, so a native filter watches for it. DPI and
    // display changes go the same path so the next apply does not reuse a stale COM object.
    class TaskbarCreatedFilter : public QAbstractNativeEventFilter {
    public:
        explicit TaskbarCreatedFilter(Private *owner) : owner_(owner) {}
        bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *) override
        {
            if (eventType != "windows_generic_MSG")
                return false;
            const auto *msg = static_cast<const MSG *>(message);
            if (!msg)
                return false;
            const HWND ours = owner_->hwndFor();
            if (owner_->taskbarCreatedMessage != 0
                && msg->message == owner_->taskbarCreatedMessage) {
                owner_->apply();
                return false;
            }
            if (ours && msg->hwnd == ours
                && (msg->message == WM_DPICHANGED || msg->message == WM_DISPLAYCHANGE)) {
                owner_->apply();
            }
            return false;
        }
    private:
        Private *owner_;
    };
    TaskbarCreatedFilter *nativeFilter = nullptr;

    HWND hwndFor() const
    {
        return window ? reinterpret_cast<HWND>(window->winId()) : nullptr;
    }
#endif

    Session acquireSession()
    {
        if (testAcquire)
            return testAcquire();
#ifdef Q_OS_WIN
        return acquireProduction();
#else
        return {};
#endif
    }

    void apply()
    {
        const bool testing = testAcquire != nullptr;
#ifdef Q_OS_WIN
        quintptr hwnd = 0;
        HICON icon = nullptr;
        const void *text = nullptr;
        if (!testing) {
            hwnd = reinterpret_cast<quintptr>(hwndFor());
            if (!hwnd || !haveHwnd)
                return;
            const QPixmap pixmap = makeTrayIcon(kind).pixmap(16, 16);
            icon = pixmapToHicon(pixmap);
            text = description.utf16();
        }
#else
        quintptr hwnd = 0;
        void *icon = nullptr;
        const void *text = nullptr;
        if (!testing)
            return;
#endif

        Session session = acquireSession();
        if (!session.handle && !session.setOverlayIcon && !session.release) {
#ifdef Q_OS_WIN
            if (icon)
                DestroyIcon(icon);
#endif
            return;
        }

        struct ReleaseGuard {
            Session session;
            ~ReleaseGuard()
            {
                if (session.release)
                    session.release(session.handle);
            }
        } guard{session};

        if (session.setOverlayIcon) {
            // Idle clears the overlay (nullptr icon). HRESULT failure: skip; the guard
            // still releases. Do not cache the pointer.
            void *overlayIcon = (kind == TrayIconKind::Idle) ? nullptr : static_cast<void *>(icon);
            (void)session.setOverlayIcon(session.handle, hwnd, overlayIcon, text);
        }
#ifdef Q_OS_WIN
        if (icon)
            DestroyIcon(icon);
#endif
    }
};

TaskbarOverlay::TaskbarOverlay(QObject *parent) : QObject(parent), d(std::make_unique<Private>()) {}

void TaskbarOverlay::setTestAcquire(AcquireFn fn)
{
    d->testAcquire = fn;
}

TaskbarOverlay::~TaskbarOverlay()
{
#ifdef Q_OS_WIN
    if (d->nativeFilter) {
        QCoreApplication::instance()->removeNativeEventFilter(d->nativeFilter);
        delete d->nativeFilter;
        d->nativeFilter = nullptr;
    }
#endif
}

bool TaskbarOverlay::supported()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

void TaskbarOverlay::attachWindow(QWindow *window)
{
    if (d->window == window)
        return;
    if (d->window)
        d->window->removeEventFilter(this);
    d->window = window;
    if (!window) {
#ifdef Q_OS_WIN
        d->haveHwnd = false;
#endif
        return;
    }
#ifdef Q_OS_WIN
    // winId() may synchronously deliver PlatformSurface / WinIdChange; resolve the HWND
    // before installing the event filter so attach applies exactly once, via reapply().
    d->haveHwnd = window->winId() != 0;
#endif
    window->installEventFilter(this);
#ifdef Q_OS_WIN
    d->taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarButtonCreated");
    if (!d->nativeFilter) {
        d->nativeFilter = new Private::TaskbarCreatedFilter(d.get());
        QCoreApplication::instance()->installNativeEventFilter(d->nativeFilter);
    }
#endif
    reapply();
}

void TaskbarOverlay::setKind(TrayIconKind kind)
{
    d->kind = kind;
    reapply();
}

void TaskbarOverlay::setDescription(const QString &text)
{
    d->description = text;
    reapply();
}

void TaskbarOverlay::reapply()
{
    d->apply();
}

bool TaskbarOverlay::eventFilter(QObject *watched, QEvent *event)
{
#ifdef Q_OS_WIN
    if (watched == d->window) {
        // HWND recreated (QEvent::PlatformSurface / SurfaceCreated) and Explorer restart
        // (TaskbarButtonCreated arrives as a QEvent::WinIdChange through Qt's translation)
        // both need the overlay re-applied to the new button. A fresh ITaskbarList3 is
        // acquired inside apply(); nothing is cached to drop here.
        if (event->type() == QEvent::PlatformSurface
            || event->type() == QEvent::WinIdChange) {
            d->haveHwnd = d->window && d->window->winId() != 0;
            d->apply();
        }
    }
#else
    Q_UNUSED(watched)
    Q_UNUSED(event)
#endif
    return QObject::eventFilter(watched, event);
}

} // namespace subrec::shell

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

// TB1 (X7). Off Windows everything is a no-op storing nothing. The Windows half is
// compile-checked with MinGW -fsyntax-only and exercised on PC3 (five states, HWND
// recreation, Explorer restart); <windows.h>/COM stay confined to this file.
class TaskbarOverlay::Private {
public:
    QWindow *window = nullptr;
    TrayIconKind kind = TrayIconKind::Idle;
    QString description;

#ifdef Q_OS_WIN
    ITaskbarList3 *taskbar = nullptr;
    UINT taskbarCreatedMessage = 0;
    bool haveHwnd = false;

    // Explorer restarts re-create the taskbar button and broadcast TaskbarButtonCreated;
    // that message never becomes a QEvent, so a native filter watches for it.
    class TaskbarCreatedFilter : public QAbstractNativeEventFilter {
    public:
        explicit TaskbarCreatedFilter(Private *owner) : owner_(owner) {}
        bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *) override
        {
            if (eventType != "windows_generic_MSG")
                return false;
            const auto *msg = static_cast<const MSG *>(message);
            if (owner_->taskbarCreatedMessage != 0
                && msg->message == owner_->taskbarCreatedMessage) {
                owner_->onTaskbarButtonCreated();
            }
            return false;
        }
    private:
        Private *owner_;
    };
    TaskbarCreatedFilter *nativeFilter = nullptr;

    void onTaskbarButtonCreated()
    {
        releaseTaskbar(); // the old button is gone
        apply();
    }

    void releaseTaskbar()
    {
        if (taskbar) {
            taskbar->Release();
            taskbar = nullptr;
        }
    }

    bool ensureTaskbar()
    {
        if (!window)
            return false;
        if (!taskbar) {
            // CoInitialize was done by QGuiApplication on the GUI thread; the threaded COM
            // apartment of Qt matches ITaskbarList3's requirements.
            if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&taskbar))))
                return false;
        }
        return true;
    }

    HWND hwndFor() const
    {
        return window ? reinterpret_cast<HWND>(window->winId()) : nullptr;
    }

    void apply()
    {
        if (!ensureTaskbar())
            return;
        const HWND hwnd = hwndFor();
        if (!hwnd || !haveHwnd)
            return;
        const QPixmap pixmap = makeTrayIcon(kind).pixmap(16, 16);
        const HICON icon = pixmapToHicon(pixmap);
        const PCWSTR text = reinterpret_cast<PCWSTR>(
            const_cast<ushort *>(description.utf16()));
        // Idle clears the overlay; the kind-to-icon mapping mirrors trayIconFor (§6.1).
        taskbar->SetOverlayIcon(hwnd, kind == TrayIconKind::Idle ? nullptr : icon, text);
        if (icon)
            DestroyIcon(icon);
    }
#endif
};

TaskbarOverlay::TaskbarOverlay(QObject *parent) : QObject(parent), d(std::make_unique<Private>()) {}

TaskbarOverlay::~TaskbarOverlay()
{
#ifdef Q_OS_WIN
    if (d->nativeFilter) {
        QCoreApplication::instance()->removeNativeEventFilter(d->nativeFilter);
        delete d->nativeFilter;
        d->nativeFilter = nullptr;
    }
    d->releaseTaskbar();
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
        d->releaseTaskbar();
        d->haveHwnd = false;
#endif
        return;
    }
    window->installEventFilter(this);
#ifdef Q_OS_WIN
    d->taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarButtonCreated");
    if (!d->nativeFilter) {
        d->nativeFilter = new Private::TaskbarCreatedFilter(d.get());
        QCoreApplication::instance()->installNativeEventFilter(d->nativeFilter);
    }
    d->haveHwnd = window->winId() != 0;
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
#ifdef Q_OS_WIN
    d->apply();
#endif
    // Nothing to do off Windows; the stored kind/description stay for a future window.
}

bool TaskbarOverlay::eventFilter(QObject *watched, QEvent *event)
{
#ifdef Q_OS_WIN
    if (watched == d->window) {
        // HWND recreated (QEvent::PlatformSurface / SurfaceCreated) and Explorer restart
        // (TaskbarButtonCreated arrives as a QEvent::WinIdChange through Qt's translation)
        // both need the overlay re-applied to the new button.
        if (event->type() == QEvent::PlatformSurface
            || event->type() == QEvent::WinIdChange) {
            d->haveHwnd = d->window && d->window->winId() != 0;
            d->releaseTaskbar(); // the old button is gone; get a fresh ITaskbarList3
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

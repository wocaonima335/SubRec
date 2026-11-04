#include "ui/backend/desktop_actions.h"

#include "ui/backend/singleton_provider.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QUrl>
#include <QDebug>
#include <QDir>
#ifdef Q_OS_WIN
#include <QProcess>
#endif

namespace subrec::ui {

namespace {
DesktopActions *s_instance = nullptr;

bool result(bool ok, const char *action)
{
    if (!ok)
        qWarning() << "[Ui]" << action << "failed";
    return ok;
}
}

DesktopActions::DesktopActions(QObject *parent)
    : QObject(parent)
{
}

DesktopActions::~DesktopActions()
{
    if (s_instance == this)
        s_instance = nullptr;
}

void DesktopActions::setInstance(DesktopActions *instance)
{
    s_instance = instance;
}

DesktopActions *DesktopActions::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(U6): QDesktopServices / QProcess explorer / QClipboard.
bool DesktopActions::openFile(const QString &path)
{
    return result(QFileInfo::exists(path)
        && QDesktopServices::openUrl(QUrl::fromLocalFile(path)), "openFile");
}

bool DesktopActions::revealInFolder(const QString &path)
{
    if (path.isEmpty())
        return result(false, "revealInFolder");
#ifdef Q_OS_WIN
    if (QProcess::startDetached(QStringLiteral("explorer.exe"),
            {QStringLiteral("/select,"), QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath())}))
        return true;
#endif
    return openFolder(QFileInfo(path).absolutePath());
}

bool DesktopActions::openFolder(const QString &path)
{
    return result(!path.isEmpty() && QFileInfo(path).isDir()
        && QDesktopServices::openUrl(QUrl::fromLocalFile(path)), "openFolder");
}

bool DesktopActions::openUrl(const QString &url)
{
    const QUrl parsed(url);
    return result((parsed.scheme() == QStringLiteral("http")
            || parsed.scheme() == QStringLiteral("https"))
        && parsed.isValid() && !parsed.host().isEmpty()
        && QDesktopServices::openUrl(parsed), "openUrl");
}

void DesktopActions::copyText(const QString &text)
{
    if (QGuiApplication::clipboard())
        QGuiApplication::clipboard()->setText(text);
}

} // namespace subrec::ui

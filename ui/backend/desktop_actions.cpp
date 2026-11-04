#include "ui/backend/desktop_actions.h"

#include "ui/backend/singleton_provider.h"

namespace subrec::ui {

namespace {
DesktopActions *s_instance = nullptr;
}

DesktopActions::DesktopActions(QObject *parent) : QObject(parent) {}
DesktopActions::~DesktopActions()
{
    if (s_instance == this) s_instance = nullptr;
}

void DesktopActions::setInstance(DesktopActions *instance) { s_instance = instance; }
DesktopActions *DesktopActions::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(U6): QDesktopServices / QProcess explorer / QClipboard.
bool DesktopActions::openFile(const QString &) { return false; }
bool DesktopActions::revealInFolder(const QString &) { return false; }
bool DesktopActions::openFolder(const QString &) { return false; }
bool DesktopActions::openUrl(const QString &) { return false; }
void DesktopActions::copyText(const QString &) {}

} // namespace subrec::ui

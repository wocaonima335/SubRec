#include "shell/taskbar_overlay.h"

namespace subrec::shell {

// TODO(TB1): ITaskbarList3 (CoCreateInstance CLSID_TaskbarList), HWND from QWindow::winId().
class TaskbarOverlay::Private {
public:
    TrayIconKind kind = TrayIconKind::Idle;
};

TaskbarOverlay::TaskbarOverlay(QObject *parent) : QObject(parent), d(std::make_unique<Private>()) {}
TaskbarOverlay::~TaskbarOverlay() = default;

bool TaskbarOverlay::supported() { return false; }
void TaskbarOverlay::attachWindow(QWindow *) {}
void TaskbarOverlay::setKind(TrayIconKind kind) { d->kind = kind; }
void TaskbarOverlay::setDescription(const QString &) {}

} // namespace subrec::shell

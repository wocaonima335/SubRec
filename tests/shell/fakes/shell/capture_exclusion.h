#pragma once
// TEST ONLY: never part of shell_module. Records ordering at the engine boundary.
#include "shell/shell_types.h"
#include <QObject>
class QWindow;
class QMenu;
namespace subrec::shell {
class CaptureExclusion {
public:
    bool capturing = false;
    ExcludeMode mode = ExcludeMode::Never;
    int windows = 0, menus = 0;
    void setCapturing(bool value) { capturing = value; }
    void setMode(ExcludeMode value) { mode = value; }
    void addWindow(QWindow *) { ++windows; }
    void addMenu(QMenu *) { ++menus; }
};
}

#ifndef SUBREC_SHELL_TRAY_ICON_FACTORY_H
#define SUBREC_SHELL_TRAY_ICON_FACTORY_H

#include "shell/shell_types.h"
#include <QIcon>

namespace subrec::shell {
// GUI thread; requires QGuiApplication. Six native pixel sizes, fixed colors.
QIcon makeTrayIcon(TrayIconKind kind);
}

#endif

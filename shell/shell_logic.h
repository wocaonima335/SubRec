#ifndef SUBREC_SHELL_LOGIC_H
#define SUBREC_SHELL_LOGIC_H

#include "shell/shell_types.h"

namespace subrec::shell {

// Pure decisions/formatting: no QObject, settings, recorder or platform access.
RecState recStateFrom(const QString &captureState, bool hasVideoDevice);
bool isCapturing(RecState state); // Preparing / Running / Paused / Stopping

// trayEnabled here means effectively enabled AND available, not just C1.
CloseOutcome decideClose(bool trayEnabled, CloseAction action);
bool hideOnMinimize(bool trayEnabled, bool minimizeToTray);
bool exitNeedsConfirm(RecState state, bool subtitleFinishing, bool confirmSetting);
int exitConfirmVariant(RecState state, bool subtitleOn); // 0=D1a, 1=D1b, 2=D1c, 3=D1d

// Allowed only while minimized/hidden and !isCapturing(state), per section 3.5.
bool notificationAllowed(WindowVisibility window, RecState state);
bool shouldExclude(ExcludeMode mode, bool capturing, bool supported);
TrayIconKind trayIconFor(RecState state);
QString trayToolTip(const ShellSnapshot &snapshot);
TrayMenuModel buildTrayMenuModel(const ShellSnapshot &snapshot);

} // namespace subrec::shell

#endif // SUBREC_SHELL_LOGIC_H

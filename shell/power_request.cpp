#include "shell/power_request.h"

#include <QDebug>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace subrec::shell {

PowerRequest::~PowerRequest()
{
    setActive(false);
}

void PowerRequest::setActive(bool active)
{
    if (active == m_active) return;
#ifdef _WIN32
    const EXECUTION_STATE flags = active
        ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)
        : ES_CONTINUOUS;
    if (SetThreadExecutionState(flags) == 0) {
        qWarning().noquote() << QStringLiteral("[Shell] SetThreadExecutionState(%1) failed, error=%2")
                                    .arg(active ? QStringLiteral("on") : QStringLiteral("off"))
                                    .arg(static_cast<qulonglong>(GetLastError()));
        // On a failed activation stay inactive; on a failed release there is nothing
        // more to do, so record the intended state either way.
        m_active = false;
        return;
    }
    qInfo().noquote() << (active ? QStringLiteral("[Shell] sleep/display-off blocked")
                                 : QStringLiteral("[Shell] sleep/display-off released"));
    m_active = active;
#endif
    // Non-Windows: no-op, never claims to be active.
}

} // namespace subrec::shell

#pragma once
// M2 (SHELL_BATCH1_REVIEW.md): AutoTest must not read or write the user's persisted
// shell preferences (HKCU\Software\SubRec\SubRec\ui on Windows). ShellSettings opens
// QSettings(QSettings::defaultFormat(), UserScope, "SubRec", "SubRec") once, in its
// constructor, so we select an INI store under an isolated directory just for that
// construction and restore the previous default format afterwards. The recorder's own
// QSettings("SubRec", "SubRec") calls use NativeFormat explicitly and are unaffected.
// Side effect: the process-wide IniFormat path stays pointed at isolatedDir (Qt offers
// no getter to restore it); the app has no other IniFormat QSettings users.
#include "shell/shell_settings.h"

#include <QSettings>
#include <QString>
#include <memory>

namespace subrec::app {

// isolatedDir empty: normal native store. Otherwise: an INI file under isolatedDir
// (the directory must outlive the returned object; start it empty so every AutoTest
// run sees the section 5.1 defaults).
inline std::unique_ptr<subrec::shell::ShellSettings>
makeShellSettings(bool excludeSupported, const QString &isolatedDir)
{
    using subrec::shell::ShellSettings;
    if (isolatedDir.isEmpty())
        return std::make_unique<ShellSettings>(excludeSupported);
    const QSettings::Format previous = QSettings::defaultFormat();
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolatedDir);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, isolatedDir);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    auto settings = std::make_unique<ShellSettings>(excludeSupported);
    QSettings::setDefaultFormat(previous);
    return settings;
}

} // namespace subrec::app

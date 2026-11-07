#include "ui/backend/settings_store.h"

namespace subrec::ui {

namespace {
QString &iniPathSlot()
{
    static QString path;
    return path;
}
} // namespace

void setUiSettingsIniPath(const QString &iniPath) { iniPathSlot() = iniPath; }
QString uiSettingsIniPath() { return iniPathSlot(); }

std::unique_ptr<QSettings> openUiSettings()
{
    const QString &path = iniPathSlot();
    if (!path.isEmpty())
        return std::make_unique<QSettings>(path, QSettings::IniFormat);
    return std::make_unique<QSettings>(QStringLiteral("SubRec"), QStringLiteral("SubRec"));
}

} // namespace subrec::ui

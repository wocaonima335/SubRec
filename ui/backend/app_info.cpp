#include "ui/backend/app_info.h"

#include "ui/backend/singleton_provider.h"

namespace subrec::ui {

namespace {
AppInfo *s_instance = nullptr;
}

AppInfo::AppInfo(QObject *parent) : QObject(parent) {}
AppInfo::~AppInfo()
{
    if (s_instance == this) s_instance = nullptr;
}

void AppInfo::setInstance(AppInfo *instance) { s_instance = instance; }
AppInfo *AppInfo::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

void AppInfo::setLogFilePath(const QString &path) { logFilePath_ = path; }
void AppInfo::setLicensePaths(const QString &license, const QString &thirdParty)
{
    licenseFilePath_ = license;
    thirdPartyLicensesPath_ = thirdParty;
}

// TODO(U7): SUBREC_VERSION / SUBREC_GIT_HASH compile definitions.
QString AppInfo::version() const { return QStringLiteral("0.0.0"); }
QString AppInfo::gitHash() const { return {}; }
QString AppInfo::versionText() const { return QStringLiteral("版本 ") + version(); }
QString AppInfo::logFilePath() const { return logFilePath_; }
QString AppInfo::licenseFilePath() const { return licenseFilePath_; }
QString AppInfo::thirdPartyLicensesPath() const { return thirdPartyLicensesPath_; }

} // namespace subrec::ui

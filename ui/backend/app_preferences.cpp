#include "ui/backend/app_preferences.h"

#include "ui/backend/singleton_provider.h"

namespace subrec::ui {

namespace {
AppPreferences *s_instance = nullptr;
}

AppPreferences::AppPreferences(QObject *parent) : QObject(parent) {}
AppPreferences::~AppPreferences()
{
    if (s_instance == this) s_instance = nullptr;
}

void AppPreferences::setInstance(AppPreferences *instance) { s_instance = instance; }
AppPreferences *AppPreferences::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(U4): persist through openUiSettings() (keys in settings_store.h).
QString AppPreferences::afterRecordAction() const { return QStringLiteral("toast"); }
void AppPreferences::setAfterRecordAction(const QString &) {}
QRect AppPreferences::windowGeometry() const { return {}; }
bool AppPreferences::windowMaximized() const { return false; }
void AppPreferences::saveWindowState(const QRect &, bool) {}

} // namespace subrec::ui

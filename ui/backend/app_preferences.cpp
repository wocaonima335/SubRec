#include "ui/backend/app_preferences.h"

#include "ui/backend/settings_store.h"
#include "ui/backend/singleton_provider.h"

#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QSettings>

namespace subrec::ui {

namespace {
AppPreferences *s_instance = nullptr;

bool isValidAfterRecord(const QString &action)
{
    return action == QLatin1String("toast") || action == QLatin1String("openFolder")
        || action == QLatin1String("library");
}
} // namespace

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

QString AppPreferences::afterRecordAction() const
{
    const QString stored = openUiSettings()->value(QStringLiteral("ui/afterRecord")).toString();
    return isValidAfterRecord(stored) ? stored : QStringLiteral("toast");
}

void AppPreferences::setAfterRecordAction(const QString &action)
{
    if (!isValidAfterRecord(action) || action == afterRecordAction())
        return;
    auto settings = openUiSettings();
    settings->setValue(QStringLiteral("ui/afterRecord"), action);
    settings->sync();
    emit changed();
}

QRect AppPreferences::windowGeometry() const
{
    const QString stored = openUiSettings()->value(QStringLiteral("ui/windowGeometry")).toString();
    // "x,y,w,h"; a null/invalid rect means first run (AppWindow centres 1280x800).
    const QStringList parts = stored.split(QLatin1Char(','));
    if (parts.size() != 4)
        return {};
    bool ok[4] = {};
    const int x = parts[0].toInt(&ok[0]);
    const int y = parts[1].toInt(&ok[1]);
    const int w = parts[2].toInt(&ok[2]);
    const int h = parts[3].toInt(&ok[3]);
    if (!(ok[0] && ok[1] && ok[2] && ok[3]) || w <= 0 || h <= 0)
        return {};
    return QRect(x, y, w, h);
}

bool AppPreferences::windowMaximized() const
{
    return openUiSettings()->value(QStringLiteral("ui/windowMaximized"), false).toBool();
}

void AppPreferences::saveWindowState(const QRect &normalGeometry, bool maximized)
{
    const QRect oldGeometry = windowGeometry();
    const bool oldMaximized = windowMaximized();
    const bool geometryChanged = normalGeometry.isValid() && normalGeometry != oldGeometry;
    const bool maximizedChanged = maximized != oldMaximized;
    if (!geometryChanged && !maximizedChanged)
        return;
    auto settings = openUiSettings();
    if (geometryChanged) {
        settings->setValue(QStringLiteral("ui/windowGeometry"),
                           QStringLiteral("%1,%2,%3,%4").arg(normalGeometry.x())
                               .arg(normalGeometry.y())
                               .arg(normalGeometry.width())
                               .arg(normalGeometry.height()));
    }
    if (maximizedChanged)
        settings->setValue(QStringLiteral("ui/windowMaximized"), maximized);
    settings->sync();
    emit changed();
}

QRect AppPreferences::clampedGeometry(const QRect &candidate) const
{
    if (!candidate.isValid())
        return {};
    if (const QGuiApplication *app = qGuiApp) {
        const QList<QScreen *> screens = app->screens();
        if (!screens.isEmpty()) {
            bool reachable = false;
            for (const QScreen *screen : screens) {
                // Require the top 80 px (title bar strip) to touch some screen.
                const QRect strip(candidate.x(), candidate.y(), candidate.width(), 80);
                if (screen->geometry().intersects(strip)) {
                    reachable = true;
                    break;
                }
            }
            if (!reachable)
                return {};
        }
    }
    return candidate;
}

} // namespace subrec::ui

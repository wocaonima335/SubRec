#include "ui/backend/theme_controller.h"

#include "ui/backend/settings_store.h"
#include "ui/backend/singleton_provider.h"

#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>

#ifdef Q_OS_WIN
#include <windows.h>
#ifndef SPI_GETCLIENTAREAANIMATION
#define SPI_GETCLIENTAREAANIMATION 0x1042
#endif
#endif

namespace subrec::ui {

namespace {
ThemeController *s_instance = nullptr;

bool isValidMode(const QString &mode)
{
    return mode == QLatin1String("system") || mode == QLatin1String("dark")
        || mode == QLatin1String("light");
}

bool systemSchemeDark()
{
    if (!qGuiApp)
        return true; // product default is dark until the app (and hints) exist
    return qGuiApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

// Windows "Animation effects" (Settings > Accessibility > Visual effects): client-area
// animation off -> honour reduce-motion. Non-Windows builds cannot read it -> false.
bool readSystemReduceMotion()
{
#ifdef Q_OS_WIN
    BOOL animated = TRUE;
    if (SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 0, &animated, 0) && !animated)
        return true;
#endif
    return false;
}
} // namespace

ThemeController::ThemeController(QObject *parent)
    : QObject(parent), mode_(QStringLiteral("system")), systemDark_(systemSchemeDark()),
      reduceMotion_(readSystemReduceMotion())
{
    const QString stored = openUiSettings()->value(QStringLiteral("ui/theme")).toString();
    if (isValidMode(stored))
        mode_ = stored;
    if (qGuiApp) {
        // Live follow: Windows app-mode switches fire colorSchemeChanged (P3-1 "跟随系统").
        connect(qGuiApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
            const bool nowDark = systemSchemeDark();
            if (nowDark != systemDark_) {
                systemDark_ = nowDark;
                emit darkChanged();
            }
        });
    }
}

ThemeController::~ThemeController()
{
    if (s_instance == this) s_instance = nullptr;
}

void ThemeController::setInstance(ThemeController *instance) { s_instance = instance; }
ThemeController *ThemeController::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

QString ThemeController::mode() const { return mode_; }

void ThemeController::setMode(const QString &mode)
{
    if (!isValidMode(mode) || mode == mode_)
        return;
    const bool wasDark = dark();
    mode_ = mode;
    {
        auto settings = openUiSettings();
        settings->setValue(QStringLiteral("ui/theme"), mode);
        settings->sync();
    }
    emit modeChanged();
    if (dark() != wasDark)
        emit darkChanged();
}

bool ThemeController::dark() const { return mode_ == QLatin1String("dark") ? true : mode_ == QLatin1String("light") ? false : systemDark_; }
bool ThemeController::systemDark() const { return systemDark_; }
bool ThemeController::reduceMotion() const { return reduceMotion_; }

} // namespace subrec::ui

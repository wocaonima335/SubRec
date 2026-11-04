#include "ui/backend/theme_controller.h"

#include "ui/backend/singleton_provider.h"

namespace subrec::ui {

namespace {
ThemeController *s_instance = nullptr;
}

ThemeController::ThemeController(QObject *parent) : QObject(parent) {}
ThemeController::~ThemeController()
{
    if (s_instance == this) s_instance = nullptr;
}

void ThemeController::setInstance(ThemeController *instance) { s_instance = instance; }
ThemeController *ThemeController::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(U1): read/write ui/theme via openUiSettings(); follow QStyleHints::colorSchemeChanged.
QString ThemeController::mode() const { return QStringLiteral("system"); }
void ThemeController::setMode(const QString &) {}
bool ThemeController::dark() const { return true; }
bool ThemeController::systemDark() const { return true; }
bool ThemeController::reduceMotion() const { return false; }

} // namespace subrec::ui

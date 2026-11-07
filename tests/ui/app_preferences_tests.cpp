// U4 unit tests: AppPreferences persistence (afterRecordAction incl. invalid values,
// windowGeometry round-trip, windowMaximized) through openUiSettings() isolation.
#include "ui/backend/app_preferences.h"
#include "ui/backend/settings_store.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>

using subrec::ui::AppPreferences;

namespace {

int g_failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir store;
    subrec::ui::setUiSettingsIniPath(store.filePath(QStringLiteral("prefs.ini")));

    // ---- defaults ----
    {
        AppPreferences prefs(nullptr);
        check(prefs.afterRecordAction() == QLatin1String("toast"), "default afterRecord toast");
        check(!prefs.windowGeometry().isValid(), "no stored geometry on first run");
        check(!prefs.windowMaximized(), "default not maximized");
    }

    // ---- afterRecordAction: write, persist, invalid ignored ----
    {
        AppPreferences prefs(nullptr);
        int changes = 0;
        QObject::connect(&prefs, &AppPreferences::changed, [&] { ++changes; });

        prefs.setAfterRecordAction(QStringLiteral("openFolder"));
        check(prefs.afterRecordAction() == QLatin1String("openFolder"), "openFolder stored");
        check(changes == 1, "changed() emitted once");

        prefs.setAfterRecordAction(QStringLiteral("openFolder"));
        check(changes == 1, "same-value write is silent");

        for (const char *bad : {"delete", "", "Toast", "library "}) {
            prefs.setAfterRecordAction(QString::fromLatin1(bad));
            check(prefs.afterRecordAction() == QLatin1String("openFolder"),
                  "invalid afterRecord write ignored");
        }
        check(changes == 1, "invalid writes emit nothing");

        prefs.setAfterRecordAction(QStringLiteral("library"));
        check(changes == 2, "library stored and announced");
    }
    {
        AppPreferences reloaded(nullptr);
        check(reloaded.afterRecordAction() == QLatin1String("library"),
              "afterRecord persists across instances");
        check(subrec::ui::openUiSettings()->value(QStringLiteral("ui/afterRecord")).toString()
                  == QLatin1String("library"),
              "stored under ui/afterRecord");
    }

    // ---- window state: geometry round-trip, maximized flag, silent no-op ----
    {
        AppPreferences prefs(nullptr);
        int changes = 0;
        QObject::connect(&prefs, &AppPreferences::changed, [&] { ++changes; });

        prefs.saveWindowState(QRect(120, 60, 1280, 800), false);
        check(prefs.windowGeometry() == QRect(120, 60, 1280, 800), "geometry round-trips");
        check(changes == 1, "geometry write announced");

        prefs.saveWindowState(QRect(120, 60, 1280, 800), false);
        check(changes == 1, "identical window state is silent");

        prefs.saveWindowState(QRect(0, 0, 960, 640), true);
        check(prefs.windowGeometry() == QRect(0, 0, 960, 640), "geometry updated");
        check(prefs.windowMaximized(), "maximized persisted");
        check(changes == 2, "maximized change announced");

        // A corrupted stored geometry must read as invalid (AppWindow falls back to the
        // default 1280x800 placement).
        subrec::ui::openUiSettings()->setValue(QStringLiteral("ui/windowGeometry"),
                                               QStringLiteral("garbage"));
        check(!prefs.windowGeometry().isValid(), "corrupt geometry reads as invalid");
    }
    {
        AppPreferences reloaded(nullptr);
        check(reloaded.windowMaximized(), "maximized persists across instances");
    }

    if (g_failures == 0) std::printf("app_preferences_tests: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}

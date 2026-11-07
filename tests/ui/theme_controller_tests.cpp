// U1 unit tests: ThemeController mode read/write, invalid-value handling, dark derivation
// and live follow of QStyleHints::colorScheme (UI_REDESIGN §2.1, P3-1 "主题").
// reduceMotion is read from Windows system settings only: on Windows the test reads the
// same setting itself and compares; on other platforms it pins the false fallback.
// No QtTest dependency: plain counters.
#include "ui/backend/settings_store.h"
#include "ui/backend/theme_controller.h"

#include <QGuiApplication>
#include <QImageReader>
#include <QStyleHints>
#include <QTemporaryDir>
#include <cstdio>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef SPI_GETCLIENTAREAANIMATION
#define SPI_GETCLIENTAREAANIMATION 0x1042
#endif
#endif

using subrec::ui::ThemeController;

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
    QGuiApplication app(argc, argv);
    QTemporaryDir store;
    subrec::ui::setUiSettingsIniPath(store.filePath(QStringLiteral("theme.ini")));

    // ---- defaults ----
    {
        ThemeController controller(nullptr);
        check(controller.mode() == QLatin1String("system"), "default mode is system");
        check(controller.systemDark() == (qGuiApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark),
              "systemDark follows QStyleHints");
        check(controller.dark() == controller.systemDark(), "system mode: dark == systemDark");
#ifdef Q_OS_WIN
        // Independent read of the Windows "Animation effects" setting the controller uses.
        // Readable: reduceMotion is its inverse. Unreadable: the controller falls back to false.
        BOOL animated = TRUE;
        if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animated, 0)) {
            check(controller.reduceMotion() == !animated,
                  "reduceMotion == !SPI_GETCLIENTAREAANIMATION when the OS setting is readable");
            if (controller.reduceMotion() != !animated)
                std::fprintf(stderr, "  SPI_GETCLIENTAREAANIMATION=%d, reduceMotion=%d\n",
                             int(animated), int(controller.reduceMotion()));
        } else {
            check(!controller.reduceMotion(),
                  "reduceMotion false when SPI_GETCLIENTAREAANIMATION cannot be read");
        }
#else
        check(!controller.reduceMotion(), "reduceMotion false when the OS setting is unreadable");
#endif
    }

    // ---- write, persistence, invalid values ----
    {
        ThemeController controller(nullptr);
        int modeCount = 0, darkCount = 0;
        QObject::connect(&controller, &ThemeController::modeChanged, [&] { ++modeCount; });
        QObject::connect(&controller, &ThemeController::darkChanged, [&] { ++darkCount; });

        const bool systemWasDark = controller.dark();
        controller.setMode(QStringLiteral("dark"));
        check(controller.mode() == QLatin1String("dark"), "setMode(dark) accepted");
        check(controller.dark(), "mode dark -> dark");
        check(modeCount == 1, "setMode(dark) emitted modeChanged once");
        check(darkCount == (systemWasDark ? 0 : 1),
              "darkChanged emitted only when the effective scheme flipped");

        controller.setMode(QStringLiteral("dark")); // same value: no signal
        check(modeCount == 1, "same-value write is a no-op");

        for (const char *bad : {"blue", "", "DARK", "System", "dark "}) {
            controller.setMode(QString::fromLatin1(bad));
            check(controller.mode() == QLatin1String("dark"), "invalid write ignored");
        }
        check(modeCount == 1, "invalid writes emit nothing");

        controller.setMode(QStringLiteral("light"));
        check(!controller.dark(), "mode light -> not dark");
        check(modeCount == 2, "setMode(light) emitted modeChanged");
    }
    {
        // A fresh instance (same ini) reads the persisted value back.
        ThemeController reloaded(nullptr);
        check(reloaded.mode() == QLatin1String("light"), "mode persists across instances");
        check(!reloaded.dark(), "persisted light stays light");
    }
    {
        // A corrupted stored value falls back to system, not to garbage.
        subrec::ui::openUiSettings()->setValue(QStringLiteral("ui/theme"), QStringLiteral("neon"));
        ThemeController reloaded(nullptr);
        check(reloaded.mode() == QLatin1String("system"), "corrupt stored mode falls back to system");
    }

    // ---- live follow of the Windows app mode ----
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    {
        // The offscreen platform used here may refuse programmatic scheme changes; probe
        // first and skip when colorScheme() does not reflect the request (the Windows app
        // mode does support it, PC3 V-item for U1 walks it live).
        qGuiApp->styleHints()->setColorScheme(Qt::ColorScheme::Dark);
        if (qGuiApp->styleHints()->colorScheme() != Qt::ColorScheme::Dark) {
            std::printf("theme_controller_tests: platform cannot set colorScheme, live-follow skipped\n");
        } else {
            ThemeController controller(nullptr);
            int darkCount = 0;
            QObject::connect(&controller, &ThemeController::darkChanged, [&] { ++darkCount; });
            controller.setMode(QStringLiteral("system"));

            check(controller.systemDark(), "colorScheme Dark observed");
            check(controller.dark(), "system mode follows Dark");
            check(darkCount >= 1, "colorScheme change emitted darkChanged");

            qGuiApp->styleHints()->setColorScheme(Qt::ColorScheme::Light);
            check(!controller.systemDark(), "colorScheme Light observed");
            check(!controller.dark(), "system mode follows Light");

            // Fixed modes ignore the system scheme.
            controller.setMode(QStringLiteral("dark"));
            qGuiApp->styleHints()->setColorScheme(Qt::ColorScheme::Light);
            check(controller.dark(), "fixed dark ignores system Light");
            qGuiApp->styleHints()->setColorScheme(Qt::ColorScheme::Unknown);
        }
    }
#else
    std::printf("theme_controller_tests: colorScheme setter unavailable, live-follow skipped\n");
#endif

    // ---- §2.8 icon set: every name rasterises, unknown names stay empty ----
    {
        const QString iconDir = QStringLiteral(":/qt/qml/SubRec/Ui/icons");
        const QStringList expected{
            QStringLiteral("monitor"), QStringLiteral("monitor-off"), QStringLiteral("camera"),
            QStringLiteral("volume-2"), QStringLiteral("mic"), QStringLiteral("mic-off"),
            QStringLiteral("captions"), QStringLiteral("circle"), QStringLiteral("square"),
            QStringLiteral("pause"), QStringLiteral("play"), QStringLiteral("video"),
            QStringLiteral("library"), QStringLiteral("settings"), QStringLiteral("folder-open"),
            QStringLiteral("external-link"), QStringLiteral("trash-2"), QStringLiteral("refresh-cw"),
            QStringLiteral("download"), QStringLiteral("x"), QStringLiteral("minus"),
            QStringLiteral("maximize-2"), QStringLiteral("minimize-2"), QStringLiteral("circle-alert"),
            QStringLiteral("circle-check"), QStringLiteral("info"), QStringLiteral("triangle-alert"),
            QStringLiteral("chevron-down"), QStringLiteral("check"), QStringLiteral("file-text"),
            QStringLiteral("app-window"), QStringLiteral("power"), QStringLiteral("keyboard"),
            QStringLiteral("grip-vertical"), QStringLiteral("search"), QStringLiteral("sliders-horizontal")};
        for (const QString &name : expected) {
            QImageReader reader(iconDir + QLatin1Char('/') + name + QStringLiteral(".svg"));
            const QSize size = reader.size();
            check(reader.canRead() && size.width() > 0, "§2.8 icon present and rasterisable");
            if (!(reader.canRead() && size.width() > 0))
                std::fprintf(stderr, "  icon: %s\n", qUtf8Printable(name));
        }
        QImageReader unknown(iconDir + QStringLiteral("/no-such-icon.svg"));
        check(!unknown.canRead(), "unknown icon name resolves to nothing");
    }

    if (g_failures == 0) std::printf("theme_controller_tests: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}

#include "shell/shell_settings.h"

#include <QSettings>

namespace subrec::shell {

class ShellSettings::Private {
public:
    explicit Private(bool supported)
        : supported(supported)
    {
        settings.setFallbacksEnabled(false);
    }
    // NativeFormat in the application (same store as QSettings("SubRec", "SubRec")).
    // Respect an explicitly selected format so tests can use isolated INI files on Windows too.
    QSettings settings{QSettings::defaultFormat(), QSettings::UserScope,
                       QStringLiteral("SubRec"), QStringLiteral("SubRec")};
    const bool supported;
};

ShellSettings::ShellSettings(bool supported, QObject *parent)
    : QObject(parent), d(std::make_unique<Private>(supported))
{
}

ShellSettings::~ShellSettings() = default;

#define BOOL_SETTING(getter, setter, key, fallback) \
    bool ShellSettings::getter() const \
    { \
        return d->settings.value(QStringLiteral(key), fallback).toBool(); \
    } \
    void ShellSettings::setter(bool value) \
    { \
        if (getter() == value) \
            return; \
        d->settings.setValue(QStringLiteral(key), value); \
        emit changed(); \
    }

BOOL_SETTING(trayEnabled, setTrayEnabled, "ui/trayEnabled", true)
BOOL_SETTING(minimizeToTray, setMinimizeToTray, "ui/minimizeToTray", false)
BOOL_SETTING(startMinimized, setStartMinimized, "ui/startMinimized", false)
BOOL_SETTING(hideOnRecordStart, setHideOnRecordStart, "ui/hideOnRecordStart", false)
BOOL_SETTING(confirmExit, setConfirmExit, "ui/confirmExitWhileRecording", true)
BOOL_SETTING(trayHintShown, setTrayHintShown, "ui/trayHintShown", false)
#undef BOOL_SETTING

QString ShellSettings::closeActionText() const
{
    const auto text = d->settings.value(QStringLiteral("ui/closeAction"), QStringLiteral("ask")).toString();
    return text == QLatin1String("tray") || text == QLatin1String("exit")
        ? text : QStringLiteral("ask");
}

void ShellSettings::setCloseActionText(const QString &text)
{
    if (text != QLatin1String("ask") && text != QLatin1String("tray") && text != QLatin1String("exit"))
        return;
    if (closeActionText() == text)
        return;
    d->settings.setValue(QStringLiteral("ui/closeAction"), text);
    emit changed();
}

CloseAction ShellSettings::closeAction() const
{
    const auto text = closeActionText();
    if (text == QLatin1String("tray"))
        return CloseAction::Tray;
    if (text == QLatin1String("exit"))
        return CloseAction::Exit;
    return CloseAction::Ask;
}

QString ShellSettings::excludeText() const
{
    const auto text = d->settings.value(QStringLiteral("ui/excludeFromCapture"),
                                        QStringLiteral("recording")).toString();
    return text == QLatin1String("always") || text == QLatin1String("never")
        ? text : QStringLiteral("recording");
}

void ShellSettings::setExcludeText(const QString &text)
{
    if (text != QLatin1String("recording") && text != QLatin1String("always") && text != QLatin1String("never"))
        return;
    if (excludeText() == text)
        return;
    d->settings.setValue(QStringLiteral("ui/excludeFromCapture"), text);
    emit changed();
}

ExcludeMode ShellSettings::excludeMode() const
{
    if (!d->supported || excludeText() == QLatin1String("never"))
        return ExcludeMode::Never;
    return excludeText() == QLatin1String("always") ? ExcludeMode::Always : ExcludeMode::RecordingOnly;
}

bool ShellSettings::excludeSupported() const
{
    return d->supported;
}

// TODO(FB1): persist ui/floatingBar, ui/barHintShown and ui/floatingBarPos/* like the
// settings above.
QString ShellSettings::floatingBarText() const
{
    if (!d->supported)
        return QStringLiteral("never");
    const auto value = d->settings.value(QStringLiteral("ui/floatingBar"),
                                          QStringLiteral("whenHidden")).toString();
    return value == QLatin1String("always") || value == QLatin1String("never")
        ? value : QStringLiteral("whenHidden");
}

void ShellSettings::setFloatingBarText(const QString &mode)
{
    if (mode != QLatin1String("whenHidden") && mode != QLatin1String("always") && mode != QLatin1String("never"))
        return;
    if (floatingBarText() == mode)
        return;
    d->settings.setValue(QStringLiteral("ui/floatingBar"), mode);
    emit changed();
}

FloatingBarMode ShellSettings::floatingBarMode() const
{
    const auto text = floatingBarText();
    if (!d->supported || text == QLatin1String("never"))
        return FloatingBarMode::Never;
    return text == QLatin1String("always") ? FloatingBarMode::Always : FloatingBarMode::WhenHidden;
}

bool ShellSettings::barHintShown() const
{
    return d->settings.value(QStringLiteral("ui/barHintShown"), false).toBool();
}

void ShellSettings::setBarHintShown(bool shown)
{
    if (barHintShown() == shown)
        return;
    d->settings.setValue(QStringLiteral("ui/barHintShown"), shown);
    emit changed();
}

std::optional<QPoint> ShellSettings::floatingBarPosition(const QString &screenKey) const
{
    if (screenKey.isEmpty())
        return std::nullopt;
    const auto key = QStringLiteral("ui/floatingBarPos/") + screenKey;
    if (!d->settings.contains(key))
        return std::nullopt;
    return d->settings.value(key).toPoint();
}

void ShellSettings::setFloatingBarPosition(const QString &screenKey, const QPoint &point)
{
    if (!screenKey.isEmpty())
        d->settings.setValue(QStringLiteral("ui/floatingBarPos/") + screenKey, point);
}

} // namespace subrec::shell

#ifndef SUBREC_UI_THEME_CONTROLLER_H
#define SUBREC_UI_THEME_CONTROLLER_H

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace subrec::ui {

// Module U1. GUI thread. QML singleton `ThemeController` (SubRec.Ui).
// Theme.qml reads `dark` to pick every colour token (UI_REDESIGN.md §2.1); no other file
// may decide light/dark on its own.
class ThemeController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // Persisted choice, ui/theme: "system" (default) | "dark" | "light" (P3-1 "主题").
    // Invalid writes are ignored.
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    // Effective scheme. "system" follows the Windows app mode via
    // QStyleHints::colorScheme() and updates live when the user switches it.
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    // What Windows currently reports, regardless of `mode` (for the settings hint).
    Q_PROPERTY(bool systemDark READ systemDark NOTIFY darkChanged)
    // Windows "animation effects" off (§2.6). False when it cannot be read; then the
    // recording dot keeps blinking. Only the blink and page fades consult it.
    Q_PROPERTY(bool reduceMotion READ reduceMotion NOTIFY reduceMotionChanged)

public:
    explicit ThemeController(QObject *parent); // no default: see singleton_provider.h
    ~ThemeController() override;

    static void setInstance(ThemeController *instance);
    static ThemeController *create(QQmlEngine *engine, QJSEngine *);

    QString mode() const;
    void setMode(const QString &mode);
    bool dark() const;
    bool systemDark() const;
    bool reduceMotion() const;

signals:
    void modeChanged();
    void darkChanged();
    void reduceMotionChanged();
};

} // namespace subrec::ui

#endif // SUBREC_UI_THEME_CONTROLLER_H

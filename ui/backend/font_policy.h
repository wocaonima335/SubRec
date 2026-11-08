#ifndef SUBREC_UI_FONT_POLICY_H
#define SUBREC_UI_FONT_POLICY_H

// §2.2 typography: one place for the UI font fallback order (Theme.qml names only the first
// family of each list). Header-only so main.cpp and tests can use it without a CMake change.
// No installed-font scan: Qt resolves the lists when it matches fonts.

#include <QFont>
#include <QGuiApplication>
#include <QStringList>

namespace subrec::ui {

inline QStringList uiFontFamilies()
{
    return {QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Segoe UI"),
            QStringLiteral("Microsoft YaHei")};
}

inline QStringList monoFontFamilies()
{
    return {QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")};
}

// Call once after the QGuiApplication exists.
// - Substitutions: QML fonts built from Theme.fontFamily / Theme.monoFamily (single names)
//   fall back along the lists when the first family is missing.
// - Application font: every Text/Control without an explicit font gets the UI family list.
//   Only the families change; the size stays the platform default, so native widgets (the
//   tray menu) keep their usual size.
inline void applyUiFontPolicy()
{
    const QStringList ui = uiFontFamilies();
    const QStringList mono = monoFontFamilies();
    QFont::insertSubstitutions(ui.first(), ui.mid(1));
    QFont::insertSubstitutions(mono.first(), mono.mid(1));
    if (qGuiApp) {
        QFont font = QGuiApplication::font();
        font.setFamilies(ui);
        QGuiApplication::setFont(font);
    }
}

} // namespace subrec::ui

#endif // SUBREC_UI_FONT_POLICY_H

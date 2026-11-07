#ifndef SUBREC_UI_DESKTOP_ACTIONS_H
#define SUBREC_UI_DESKTOP_ACTIONS_H

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace subrec::ui {

// Module U6. GUI thread. QML singleton `DesktopActions`: system shell actions used across
// pages. All return false (and log "[Ui]") on failure; callers show T3 when it matters.
class DesktopActions : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit DesktopActions(QObject *parent); // no default: see singleton_provider.h
    ~DesktopActions() override;

    static void setInstance(DesktopActions *instance);
    static DesktopActions *create(QQmlEngine *engine, QJSEngine *);

    // Default program (QDesktopServices::openUrl(fromLocalFile)): play a recording, open .srt,
    // open LICENSE / THIRD_PARTY_LICENSES.md.
    Q_INVOKABLE bool openFile(const QString &absolutePath);
    // Explorer with the file selected ("explorer /select,<path>"); falls back to the folder.
    Q_INVOKABLE bool revealInFolder(const QString &absolutePath);
    Q_INVOKABLE bool openFolder(const QString &absolutePath);
    Q_INVOKABLE bool openUrl(const QString &url); // http(s) only (licence links)
    // Clipboard (QGuiApplication::clipboard()); S6 "复制错误信息", P2 "复制文件路径".
    Q_INVOKABLE void copyText(const QString &text);
};

} // namespace subrec::ui

#endif // SUBREC_UI_DESKTOP_ACTIONS_H

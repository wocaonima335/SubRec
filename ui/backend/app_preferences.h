#ifndef SUBREC_UI_APP_PREFERENCES_H
#define SUBREC_UI_APP_PREFERENCES_H

#include <QObject>
#include <QRect>
#include <QString>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace subrec::ui {

// Module U4. GUI thread. QML singleton `AppPreferences`.
// New-UI preferences that are NOT shell (C1–C7 stay in ShellSettings, reached via ShellView)
// and NOT output naming (OutputNaming). Persisted through openUiSettings().
class AppPreferences : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // P3-1 "录制结束后", ui/afterRecord: "toast" (default, only T1) | "openFolder" | "library".
    // T1 is shown in every case (§3.7); AppWindow performs the extra action on recordingSaved.
    Q_PROPERTY(QString afterRecordAction READ afterRecordAction WRITE setAfterRecordAction NOTIFY changed)
    // R3: last normal (non-maximized) geometry in logical px, ui/windowGeometry.
    // Null rect on first run: AppWindow uses 1280x800 centred on the primary screen and
    // clamps a restored rect to a visible screen (min 960x640, §2.7).
    Q_PROPERTY(QRect windowGeometry READ windowGeometry NOTIFY changed)
    // ui/windowMaximized, default false.
    Q_PROPERTY(bool windowMaximized READ windowMaximized NOTIFY changed)

public:
    explicit AppPreferences(QObject *parent); // no default: see singleton_provider.h
    ~AppPreferences() override;

    static void setInstance(AppPreferences *instance);
    static AppPreferences *create(QQmlEngine *engine, QJSEngine *);

    QString afterRecordAction() const;
    void setAfterRecordAction(const QString &action);
    QRect windowGeometry() const;
    bool windowMaximized() const;

    // Called by AppWindow when the window is about to hide/close and after a move/resize
    // settles (AppWindow debounces, >= 500 ms). Writes both keys; emits changed() only if
    // a value actually changed.
    Q_INVOKABLE void saveWindowState(const QRect &normalGeometry, bool maximized);

    // R3 helper: returns `candidate` unchanged when it is at least partially on a
    // connected screen (so restoring it keeps the window reachable); otherwise an invalid
    // rect and the caller falls back to the default 1280x800 placement.
    Q_INVOKABLE QRect clampedGeometry(const QRect &candidate) const;

signals:
    void changed();
};

} // namespace subrec::ui

#endif // SUBREC_UI_APP_PREFERENCES_H

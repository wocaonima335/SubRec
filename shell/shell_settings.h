#ifndef SUBREC_SHELL_SETTINGS_H
#define SUBREC_SHELL_SETTINGS_H

#include "shell/shell_types.h"

#include <QObject>
#include <QPoint>
#include <memory>
#include <optional>

namespace subrec::shell {

// GUI thread only. QML context name: shellSettings.
// Persistence: QSettings("SubRec", "SubRec"), ui/* keys from section 5.1.
class ShellSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool trayEnabled READ trayEnabled WRITE setTrayEnabled NOTIFY changed)
    Q_PROPERTY(bool minimizeToTray READ minimizeToTray WRITE setMinimizeToTray NOTIFY changed)
    Q_PROPERTY(QString closeAction READ closeActionText WRITE setCloseActionText NOTIFY changed)
    Q_PROPERTY(bool startMinimized READ startMinimized WRITE setStartMinimized NOTIFY changed)
    Q_PROPERTY(QString excludeFromCapture READ excludeText WRITE setExcludeText NOTIFY changed)
    Q_PROPERTY(bool hideOnRecordStart READ hideOnRecordStart WRITE setHideOnRecordStart NOTIFY changed)
    Q_PROPERTY(bool confirmExitWhileRecording READ confirmExit WRITE setConfirmExit NOTIFY changed)
    Q_PROPERTY(bool excludeSupported READ excludeSupported CONSTANT)
    // Batch 2 (module FB1), C5 ui/floatingBar: "whenHidden" | "always" | "never". Default
    // "whenHidden", but "never" when !excludeSupported (§5.1). Invalid writes ignored.
    Q_PROPERTY(QString floatingBar READ floatingBarText WRITE setFloatingBarText NOTIFY changed)

public:
    // Inject CaptureExclusion::supported(); this capability is immutable.
    explicit ShellSettings(bool excludeSupported, QObject *parent = nullptr);
    ~ShellSettings() override;

    bool trayEnabled() const; // C1: true
    void setTrayEnabled(bool enabled);
    bool minimizeToTray() const; // C2: false
    void setMinimizeToTray(bool enabled);
    QString closeActionText() const; // C3: "ask"; accepts ask | tray | exit
    void setCloseActionText(const QString &action);
    CloseAction closeAction() const;
    bool startMinimized() const; // C4: false
    void setStartMinimized(bool enabled);
    QString excludeText() const; // C6: "recording"; recording | always | never
    void setExcludeText(const QString &mode);
    ExcludeMode excludeMode() const; // Effective mode: Never if unsupported.
    bool hideOnRecordStart() const; // C7: false
    void setHideOnRecordStart(bool enabled);
    bool confirmExit() const; // ui/confirmExitWhileRecording: true
    void setConfirmExit(bool enabled);
    bool excludeSupported() const;
    bool trayHintShown() const; // ui/trayHintShown: false
    void setTrayHintShown(bool shown);
    // Batch 2 (module FB1). Stubs until FB1 lands: defaults only, setters do not persist.
    QString floatingBarText() const;
    void setFloatingBarText(const QString &mode);
    FloatingBarMode floatingBarMode() const; // effective: Never if unsupported
    bool barHintShown() const;               // ui/barHintShown: false (FB-H shown once)
    void setBarHintShown(bool shown);
    // ui/floatingBarPos/<screenKey>: remembered bar top-left (logical px, global) per screen.
    // nullopt when never saved; FloatingBarPolicy then falls back to the §3.6 default.
    // Not a QML property and does not emit changed() (written on every drag release).
    std::optional<QPoint> floatingBarPosition(const QString &screenKey) const;
    void setFloatingBarPosition(const QString &screenKey, const QPoint &topLeft);

signals:
    // Emitted when a stored setting changes; backs every mutable QML property.
    void changed();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace subrec::shell

#endif // SUBREC_SHELL_SETTINGS_H

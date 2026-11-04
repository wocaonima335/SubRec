#ifndef SUBREC_SHELL_FLOATING_BAR_POLICY_H
#define SUBREC_SHELL_FLOATING_BAR_POLICY_H

#include "shell/shell_types.h"

#include <QObject>
#include <QPoint>
#include <QString>
#include <memory>
#include <optional>

namespace subrec::shell {

// Batch 2, module FB1. When the mini floating bar is shown (MINIMIZE_DESIGN §3.6, C5).
struct FloatingBarInputs {
    RecState state = RecState::Idle;
    WindowVisibility window = WindowVisibility::Shown;
    FloatingBarMode mode = FloatingBarMode::WhenHidden; // effective (Never if unsupported, see ShellSettings)
    std::optional<bool> trayOverride;                    // tray item 6, this recording only
    bool dismissedAfterError = false;                    // FB-6 closed by the user
    bool savedFlashActive = false;                       // FB-5 "已保存" 2 s after Stopping -> Idle
};

// Pure. Rules: shown only in Preparing/Running/Paused/Stopping (plus the FB-5 flash and FB-6
// until dismissed); WhenHidden additionally requires window W2/W3; Always ignores window;
// Never hides; trayOverride, when set, replaces the mode decision for capturing states.
bool floatingBarShouldShow(const FloatingBarInputs &in);

class ShellSettings;

// Batch 2, module FB1. GUI thread. Owns the floating bar's runtime state; ui::FloatingBarView
// reads it, main.cpp feeds it. All persistence goes through ShellSettings (ui/floatingBar,
// ui/barHintShown, ui/floatingBarPos/<screenKey>), so AutoTest isolation follows ShellSettings.
class FloatingBarPolicy : public QObject {
    Q_OBJECT

public:
    explicit FloatingBarPolicy(QObject *parent = nullptr);
    ~FloatingBarPolicy() override;

    // Borrowed, must outlive this object. Supplies the effective mode (following
    // ShellSettings::changed), barHintShown and positions. Null (tests): mode comes from
    // setMode(), the hint is never pending and positions are not remembered.
    void setSettings(ShellSettings *settings);

    // Inputs (GUI thread). setSnapshot comes from ShellStateSource/WindowController changes.
    void setSnapshot(const ShellSnapshot &snapshot);
    void setMode(FloatingBarMode mode);
    // Tray item 6 toggled. Cleared automatically when the state returns to Idle/Failed.
    void setTrayOverride(bool show);
    void dismiss(); // FB-6 close button

    bool shouldShow() const;
    // Bar state for QML: "preparing" | "running" | "paused" | "stopping" | "saved" | "failed".
    QString barState() const;
    // FB-H: first time the main window is hidden while capturing and ui/barHintShown is false.
    bool hintPending() const;
    void acknowledgeHint(); // writes ui/barHintShown=true

    // Remembered top-left (logical px, global) for a screen; nullopt -> caller uses
    // "top centre of the recorded screen, 16 px from the top" (§3.6).
    std::optional<QPoint> savedPosition(const QString &screenKey) const;
    void savePosition(const QString &screenKey, const QPoint &topLeft);

signals:
    void shouldShowChanged();
    void barStateChanged();
    void hintPendingChanged();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace subrec::shell

#endif // SUBREC_SHELL_FLOATING_BAR_POLICY_H

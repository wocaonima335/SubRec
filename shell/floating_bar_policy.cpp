#include "shell/floating_bar_policy.h"

#include "shell/shell_logic.h"
#include "shell/shell_settings.h"

#include <QPointer>
#include <QTimer>

namespace subrec::shell {

// TODO(FB1): implement the §3.6 rules and cover them in tests/ui/floating_bar_policy_tests.cpp
// (registered in tests/ui/tests_codex.cmake).
bool floatingBarShouldShow(const FloatingBarInputs &inputs)
{
    const bool capturing = isCapturing(inputs.state);
    if (capturing && inputs.trayOverride.has_value())
        return *inputs.trayOverride;
    if (inputs.mode == FloatingBarMode::Never)
        return false;
    if (inputs.state == RecState::Failed && inputs.dismissedAfterError)
        return false;
    if (!capturing && !inputs.savedFlashActive && inputs.state != RecState::Failed)
        return false;
    return inputs.mode == FloatingBarMode::Always
        || inputs.window != WindowVisibility::Shown;
}

class FloatingBarPolicy::Private {
public:
    ShellSnapshot snapshot;
    FloatingBarMode mode = FloatingBarMode::WhenHidden;
    std::optional<bool> trayOverride;
    bool dismissed = false;
    bool savedFlash = false;
    QTimer flashTimer;
    QPointer<ShellSettings> settings;
    bool hintAcknowledged = false;
};

FloatingBarPolicy::FloatingBarPolicy(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->flashTimer.setSingleShot(true);
    d->flashTimer.setInterval(2000);
    connect(&d->flashTimer, &QTimer::timeout, this, [this] {
        const bool wasVisible = shouldShow();
        d->savedFlash = false;
        if (wasVisible != shouldShow())
            emit shouldShowChanged();
        emit barStateChanged();
    });
}

FloatingBarPolicy::~FloatingBarPolicy() = default;

void FloatingBarPolicy::setSettings(ShellSettings *settings)
{
    if (d->settings)
        disconnect(d->settings, nullptr, this, nullptr);
    d->settings = settings;
    if (settings) {
        setMode(settings->floatingBarMode());
        d->hintAcknowledged = settings->barHintShown();
        connect(settings, &ShellSettings::changed, this, [this] {
            setMode(d->settings->floatingBarMode());
            d->hintAcknowledged = d->settings->barHintShown();
            emit hintPendingChanged();
        });
    }
}

void FloatingBarPolicy::setSnapshot(const ShellSnapshot &snapshot)
{
    const bool oldVisible = shouldShow();
    const auto oldState = barState();
    if (snapshot.state == RecState::Idle || snapshot.state == RecState::Failed)
        d->trayOverride.reset();
    if (snapshot.state != RecState::Failed)
        d->dismissed = false;
    if (snapshot.state != d->snapshot.state) {
        d->savedFlash = snapshot.state == RecState::Idle
            && d->snapshot.state == RecState::Stopping && oldVisible;
        if (d->savedFlash)
            d->flashTimer.start();
        else
            d->flashTimer.stop();
        if (snapshot.state == RecState::Failed)
            d->dismissed = !oldVisible;
    }
    d->snapshot = snapshot;
    if (oldVisible != shouldShow())
        emit shouldShowChanged();
    if (oldState != barState())
        emit barStateChanged();
    emit hintPendingChanged();
}

void FloatingBarPolicy::setMode(FloatingBarMode mode)
{
    if (d->mode == mode)
        return;
    const bool oldVisible = shouldShow();
    d->mode = mode;
    if (oldVisible != shouldShow())
        emit shouldShowChanged();
}

void FloatingBarPolicy::setTrayOverride(bool show)
{
    const bool oldVisible = shouldShow();
    d->trayOverride = show;
    if (oldVisible != shouldShow())
        emit shouldShowChanged();
}

void FloatingBarPolicy::dismiss()
{
    const bool oldVisible = shouldShow();
    d->dismissed = true;
    if (oldVisible != shouldShow())
        emit shouldShowChanged();
}

bool FloatingBarPolicy::shouldShow() const
{
    return floatingBarShouldShow({d->snapshot.state, d->snapshot.window, d->mode,
                                 d->trayOverride, d->dismissed, d->savedFlash});
}

QString FloatingBarPolicy::barState() const
{
    if (d->savedFlash)
        return QStringLiteral("saved");
    switch (d->snapshot.state) {
    case RecState::Preparing: return QStringLiteral("preparing");
    case RecState::Running: return QStringLiteral("running");
    case RecState::Paused: return QStringLiteral("paused");
    case RecState::Stopping: return QStringLiteral("stopping");
    case RecState::Failed: return QStringLiteral("failed");
    default: return {};
    }
}

bool FloatingBarPolicy::hintPending() const
{
    return d->settings && !d->hintAcknowledged
        && d->snapshot.window != WindowVisibility::Shown
        && isCapturing(d->snapshot.state);
}

void FloatingBarPolicy::acknowledgeHint()
{
    if (d->hintAcknowledged)
        return;
    d->hintAcknowledged = true;
    if (d->settings)
        d->settings->setBarHintShown(true);
    emit hintPendingChanged();
}

std::optional<QPoint> FloatingBarPolicy::savedPosition(const QString &screenKey) const
{
    return d->settings ? d->settings->floatingBarPosition(screenKey) : std::nullopt;
}

void FloatingBarPolicy::savePosition(const QString &screenKey, const QPoint &topLeft)
{
    if (d->settings)
        d->settings->setFloatingBarPosition(screenKey, topLeft);
}

} // namespace subrec::shell

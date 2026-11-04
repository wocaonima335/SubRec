#include "shell/floating_bar_policy.h"

namespace subrec::shell {

// TODO(FB1): implement the §3.6 rules and cover them in tests/ui/floating_bar_policy_tests.cpp
// (registered in tests/ui/tests_codex.cmake).
bool floatingBarShouldShow(const FloatingBarInputs &) { return false; }

class FloatingBarPolicy::Private {
public:
    ShellSnapshot snapshot;
    FloatingBarMode mode = FloatingBarMode::WhenHidden;
};

FloatingBarPolicy::FloatingBarPolicy(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>()) {}
FloatingBarPolicy::~FloatingBarPolicy() = default;

void FloatingBarPolicy::setSettings(ShellSettings *) {}
void FloatingBarPolicy::setSnapshot(const ShellSnapshot &snapshot) { d->snapshot = snapshot; }
void FloatingBarPolicy::setMode(FloatingBarMode mode) { d->mode = mode; }
void FloatingBarPolicy::setTrayOverride(bool) {}
void FloatingBarPolicy::dismiss() {}
bool FloatingBarPolicy::shouldShow() const { return false; }
QString FloatingBarPolicy::barState() const { return QStringLiteral("running"); }
bool FloatingBarPolicy::hintPending() const { return false; }
void FloatingBarPolicy::acknowledgeHint() {}
std::optional<QPoint> FloatingBarPolicy::savedPosition(const QString &) const { return std::nullopt; }
void FloatingBarPolicy::savePosition(const QString &, const QPoint &) {}

} // namespace subrec::shell

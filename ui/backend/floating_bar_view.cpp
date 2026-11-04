#include "ui/backend/floating_bar_view.h"

#include "shell/floating_bar_policy.h"
#include "ui/backend/singleton_provider.h"

#include <QQuickWindow>

namespace subrec::ui {

namespace {
FloatingBarView *s_instance = nullptr;
}

FloatingBarView::FloatingBarView(QObject *parent) : QObject(parent) {}
FloatingBarView::~FloatingBarView()
{
    if (s_instance == this) s_instance = nullptr;
}

void FloatingBarView::setInstance(FloatingBarView *instance) { s_instance = instance; }
FloatingBarView *FloatingBarView::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(FB2): mirror FloatingBarPolicy signals, register the bar window, positions.
void FloatingBarView::attach(shell::FloatingBarPolicy *policy, Actions actions)
{
    policy_ = policy;
    actions_ = std::move(actions);
}
void FloatingBarView::setLiveTexts(const QString &, const QString &, const QString &) {}

bool FloatingBarView::shown() const { return false; }
QString FloatingBarView::barState() const { return QStringLiteral("running"); }
QString FloatingBarView::elapsedText() const { return QStringLiteral("00:00:00"); }
QString FloatingBarView::captureError() const { return {}; }
QString FloatingBarView::subtitleIndicator() const { return {}; }
bool FloatingBarView::hintVisible() const { return false; }

void FloatingBarView::attachWindow(QQuickWindow *) {}
void FloatingBarView::pauseResume() {}
void FloatingBarView::stop() {}
void FloatingBarView::openMainWindow() {}
void FloatingBarView::dismiss() {}
void FloatingBarView::acknowledgeHint() {}
QPoint FloatingBarView::preferredPosition(int, int) const { return {}; }
void FloatingBarView::savePosition(int, int) {}

} // namespace subrec::ui

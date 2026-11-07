#include "ui/backend/floating_bar_view.h"

#include "shell/floating_bar_policy.h"
#include "ui/backend/singleton_provider.h"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

namespace subrec::ui {

namespace {
FloatingBarView *s_instance = nullptr;

// §3.6: the bar snaps to an edge when released within 24 px of it.
constexpr int kSnapDistance = 24;
constexpr int kTopMargin = 16;
// §6.3 FB-H: the hint bubble disappears after 8 s on its own.
constexpr int kHintTimeoutMs = 8000;

QString screenKeyFor(const QQuickWindow *bar)
{
    if (bar && bar->screen())
        return bar->screen()->name();
    if (const QScreen *primary = QGuiApplication::primaryScreen())
        return primary->name();
    return QStringLiteral("primary");
}
} // namespace

// FB2. The window contract: attachWindow() forces the native window (winId) and hands it
// to CaptureExclusion BEFORE the first show, so the bar's first frame is already excluded
// (MINIMIZE_DESIGN §4.4 item 4). Off Windows the exclusion callback is a no-op from
// batch-1's CaptureExclusion; winId() is still forced so the window is created early.
FloatingBarView::FloatingBarView(QObject *parent)
    : QObject(parent), hintTimer_(std::make_unique<QTimer>(this))
{
    hintTimer_->setSingleShot(true);
    hintTimer_->setInterval(kHintTimeoutMs);
    connect(hintTimer_.get(), &QTimer::timeout, this, &FloatingBarView::acknowledgeHint);
}

FloatingBarView::~FloatingBarView()
{
    if (s_instance == this) s_instance = nullptr;
}

void FloatingBarView::setInstance(FloatingBarView *instance) { s_instance = instance; }
FloatingBarView *FloatingBarView::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

void FloatingBarView::attach(shell::FloatingBarPolicy *policy, Actions actions)
{
    policy_ = policy;
    actions_ = std::move(actions);
    if (policy_) {
        connect(policy_, &shell::FloatingBarPolicy::shouldShowChanged, this,
                &FloatingBarView::shownChanged);
        connect(policy_, &shell::FloatingBarPolicy::barStateChanged, this,
                &FloatingBarView::barStateChanged);
        connect(policy_, &shell::FloatingBarPolicy::hintPendingChanged, this, [this] {
            if (hintVisible())
                hintTimer_->start();
            else
                hintTimer_->stop();
            emit hintVisibleChanged();
        });
        if (hintVisible())
            hintTimer_->start();
    }
}

void FloatingBarView::setLiveTexts(const QString &elapsedText, const QString &captureError,
                                   const QString &subtitleIndicator)
{
    elapsedText_ = elapsedText;
    captureError_ = captureError;
    subtitleIndicator_ = subtitleIndicator;
    emit barStateChanged();
}

bool FloatingBarView::shown() const { return policy_ && policy_->shouldShow(); }
QString FloatingBarView::barState() const
{
    return policy_ ? policy_->barState() : QStringLiteral("running");
}
QString FloatingBarView::elapsedText() const { return elapsedText_; }
QString FloatingBarView::captureError() const { return captureError_; }
QString FloatingBarView::subtitleIndicator() const { return subtitleIndicator_; }
bool FloatingBarView::hintVisible() const
{
    return policy_ && policy_->hintPending() && shown();
}

void FloatingBarView::attachWindow(QQuickWindow *bar)
{
    if (!bar || bar_ == bar)
        return;
    bar_ = bar;
    // Force the native window now, then exclude it before it can ever be shown.
    bar->create();
    if (actions_.registerExcluded)
        actions_.registerExcluded(bar);
}

void FloatingBarView::pauseResume()
{
    if (actions_.pauseResume) actions_.pauseResume();
}

void FloatingBarView::stop()
{
    if (actions_.stop) actions_.stop();
}

void FloatingBarView::openMainWindow()
{
    if (actions_.openMainWindow) actions_.openMainWindow();
}

void FloatingBarView::dismiss()
{
    if (policy_) policy_->dismiss();
}

void FloatingBarView::acknowledgeHint()
{
    hintTimer_->stop();
    if (policy_)
        policy_->acknowledgeHint();
}

QPoint FloatingBarView::preferredPosition(int barWidth, int barHeight) const
{
    Q_UNUSED(barHeight) // the default sits 16 px from the top regardless of the height
    const QString key = screenKeyFor(bar_);
    if (policy_) {
        if (const auto saved = policy_->savedPosition(key))
            return *saved;
    }
    // §3.6 default: top centre of the current screen, 16 px from the top edge. (Which
    // screen is being recorded is not exposed to the UI layer yet; the bar lives on the
    // screen the user last dropped it, which starts as the primary screen.)
    const QScreen *screen = bar_ && bar_->screen() ? bar_->screen()
                                                   : QGuiApplication::primaryScreen();
    if (!screen)
        return {};
    const QRect area = screen->availableGeometry();
    return QPoint(area.center().x() - barWidth / 2, area.top() + kTopMargin);
}

void FloatingBarView::savePosition(int x, int y)
{
    QPoint point(x, y);
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (const QScreen *screen : screens) {
        const QRect area = screen->availableGeometry();
        const bool near = point.x() >= area.left() - kSnapDistance
            && point.x() <= area.right() + kSnapDistance
            && point.y() >= area.top() - kSnapDistance
            && point.y() <= area.bottom() + kSnapDistance;
        if (!near)
            continue;
        // Snap only while within 24 px of an edge (§3.6).
        if (qAbs(point.x() - area.left()) <= kSnapDistance)
            point.setX(area.left());
        else if (qAbs(point.x() + barWidthFallback() - area.right()) <= kSnapDistance)
            point.setX(area.right() - barWidthFallback() + 1);
        if (qAbs(point.y() - area.top()) <= kSnapDistance)
            point.setY(area.top());
        else if (qAbs(point.y() + 40 - area.bottom()) <= kSnapDistance)
            point.setY(area.bottom() - 40 + 1);
        break;
    }
    if (policy_)
        policy_->savePosition(screenKeyFor(bar_), point);
    if (bar_ && bar_->isVisible())
        bar_->setPosition(point);
}

int FloatingBarView::barWidthFallback() const { return bar_ ? bar_->width() : 264; }

} // namespace subrec::ui

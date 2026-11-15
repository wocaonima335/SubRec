#include "ui/backend/window_resize_handler.h"

#include <QMouseEvent>
#include <algorithm>

namespace subrec::ui {
namespace {

bool validEdges(Qt::Edges edges)
{
    const Qt::Edges horizontal = edges & (Qt::LeftEdge | Qt::RightEdge);
    const Qt::Edges vertical = edges & (Qt::TopEdge | Qt::BottomEdge);
    return edges != Qt::Edges{} && (horizontal | vertical) == edges
        && horizontal != (Qt::LeftEdge | Qt::RightEdge)
        && vertical != (Qt::TopEdge | Qt::BottomEdge);
}

} // namespace

WindowResizeHandler::WindowResizeHandler(QObject *parent) : QObject(parent)
{
}

WindowResizeHandler::~WindowResizeHandler()
{
    finish();
}

bool WindowResizeHandler::active() const
{
    return active_;
}

bool WindowResizeHandler::usesManualResize()
{
#ifdef Q_OS_WIN
    // Avoid the native SC_SIZE modal loop for the frameless Qt Quick/OpenGL surface.
    // This also keeps Win10 and Win11 on the same, testable edge-resize path.
    return true;
#else
    return false;
#endif
}

bool WindowResizeHandler::start(QWindow *window, int edges, const QPointF &globalPosition)
{
    finish();
    const auto selectedEdges = Qt::Edges(edges);
    if (!window || window->visibility() != QWindow::Windowed || !validEdges(selectedEdges))
        return false;
    if (usesManualResize())
        return begin(window, selectedEdges, globalPosition);
    return window->startSystemResize(selectedEdges);
}

bool WindowResizeHandler::begin(QWindow *window, Qt::Edges edges, const QPointF &globalPosition)
{
    finish();
    if (!window || window->visibility() != QWindow::Windowed
        || !window->flags().testFlag(Qt::FramelessWindowHint) || !validEdges(edges))
        return false;
    window_ = window;
    initialGeometry_ = window->geometry();
    initialPosition_ = globalPosition;
    edges_ = edges;
    window->installEventFilter(this);
    destroyedConnection_ = connect(window, &QObject::destroyed, this, &WindowResizeHandler::finish);
    active_ = true;
    emit activeChanged();
    return true;
}

void WindowResizeHandler::finish()
{
    if (window_)
        window_->removeEventFilter(this);
    disconnect(destroyedConnection_);
    window_.clear();
    if (!active_)
        return;
    active_ = false;
    emit activeChanged();
}

void WindowResizeHandler::updateGeometry(const QPointF &globalPosition)
{
    if (!window_)
        return;
    // Use the press-time rectangle and global logical coordinates. Local positions
    // change under the pointer when dragging the top/left edge and must not accumulate.
    const QPoint delta = (globalPosition - initialPosition_).toPoint();
    QRect next = initialGeometry_;
    if (edges_.testFlag(Qt::LeftEdge) || edges_.testFlag(Qt::RightEdge)) {
        const bool left = edges_.testFlag(Qt::LeftEdge);
        const int minimum = std::max(1, window_->minimumWidth());
        const int maximum = std::max(minimum, window_->maximumWidth());
        const int width = std::clamp(initialGeometry_.width() + (left ? -delta.x() : delta.x()),
                                     minimum, maximum);
        next.setWidth(width);
        if (left)
            next.moveLeft(initialGeometry_.x() + initialGeometry_.width() - width);
    }
    if (edges_.testFlag(Qt::TopEdge) || edges_.testFlag(Qt::BottomEdge)) {
        const bool top = edges_.testFlag(Qt::TopEdge);
        const int minimum = std::max(1, window_->minimumHeight());
        const int maximum = std::max(minimum, window_->maximumHeight());
        const int height = std::clamp(initialGeometry_.height() + (top ? -delta.y() : delta.y()),
                                      minimum, maximum);
        next.setHeight(height);
        if (top)
            next.moveTop(initialGeometry_.y() + initialGeometry_.height() - height);
    }
    if (next != window_->geometry())
        window_->setGeometry(next);
}

bool WindowResizeHandler::eventFilter(QObject *watched, QEvent *event)
{
    if (!active_ || watched != window_)
        return false;
    switch (event->type()) {
    case QEvent::MouseMove: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->buttons().testFlag(Qt::LeftButton))
            updateGeometry(mouse->globalPosition());
        else
            finish();
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            updateGeometry(mouse->globalPosition());
            finish();
        }
        break;
    }
    case QEvent::UngrabMouse:
    case QEvent::WindowDeactivate:
    case QEvent::Hide:
    case QEvent::WindowStateChange:
    case QEvent::Close:
        finish();
        break;
    default:
        break;
    }
    // Qt Quick still owns the MouseArea grab, release, hover and cancellation events.
    return false;
}

} // namespace subrec::ui

#ifndef SUBREC_UI_WINDOW_RESIZE_HANDLER_H
#define SUBREC_UI_WINDOW_RESIZE_HANDLER_H

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

namespace subrec::ui {

// Edge resizing only. Title-bar movement continues to use startSystemMove().
class WindowResizeHandler : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool usesManualResize READ usesManualResize CONSTANT)

public:
    explicit WindowResizeHandler(QObject *parent = nullptr);
    ~WindowResizeHandler() override;

    bool active() const;
    static bool usesManualResize();
    Q_INVOKABLE bool start(QWindow *window, int edges, const QPointF &globalPosition);
    Q_INVOKABLE void finish();

    // Portable geometry/session implementation, also exercised by offscreen tests.
    // start() selects it only on Windows; other platforms use system resizing.
    bool begin(QWindow *window, Qt::Edges edges, const QPointF &globalPosition);

signals:
    void activeChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateGeometry(const QPointF &globalPosition);
    QPointer<QWindow> window_;
    QMetaObject::Connection destroyedConnection_;
    QRect initialGeometry_;
    QPointF initialPosition_;
    Qt::Edges edges_;
    bool active_ = false;
};

} // namespace subrec::ui

#endif // SUBREC_UI_WINDOW_RESIZE_HANDLER_H

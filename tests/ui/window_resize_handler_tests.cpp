#include "ui/backend/window_resize_handler.h"

#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/QQmlExtensionPlugin>
#include <QtTest>
#include <memory>

Q_IMPORT_QML_PLUGIN(SubRec_UiPlugin)

using subrec::ui::WindowResizeHandler;

namespace {

void prepare(QWindow &window)
{
    window.setFlags(Qt::Window | Qt::FramelessWindowHint);
    window.setMinimumSize(QSize(320, 240));
    window.setMaximumSize(QSize(1000, 800));
    window.setGeometry(100, 100, 640, 480);
    window.show();
    QCoreApplication::processEvents();
}

void mouseEvent(QWindow &window, QEvent::Type type, const QPointF &global,
                Qt::MouseButton button = Qt::NoButton, Qt::MouseButtons buttons = Qt::LeftButton)
{
    QMouseEvent event(type, QPointF(10, 10), global, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &event);
}

} // namespace

class WindowResizeHandlerTests : public QObject {
    Q_OBJECT

private slots:
    void platformPolicy()
    {
#ifdef Q_OS_WIN
        QVERIFY(WindowResizeHandler::usesManualResize());
#else
        QVERIFY(!WindowResizeHandler::usesManualResize());
#endif
    }

    void edges_data()
    {
        QTest::addColumn<int>("edges");
        QTest::addColumn<QPoint>("delta");
        QTest::addColumn<QRect>("expected");
        QTest::newRow("top") << int(Qt::TopEdge) << QPoint(30, 40) << QRect(100, 140, 640, 440);
        QTest::newRow("bottom") << int(Qt::BottomEdge) << QPoint(30, 40) << QRect(100, 100, 640, 520);
        QTest::newRow("left") << int(Qt::LeftEdge) << QPoint(30, 40) << QRect(130, 100, 610, 480);
        QTest::newRow("right") << int(Qt::RightEdge) << QPoint(30, 40) << QRect(100, 100, 670, 480);
        QTest::newRow("top-left") << int(Qt::TopEdge | Qt::LeftEdge)
                                  << QPoint(30, 40) << QRect(130, 140, 610, 440);
        QTest::newRow("top-right") << int(Qt::TopEdge | Qt::RightEdge)
                                   << QPoint(30, 40) << QRect(100, 140, 670, 440);
        QTest::newRow("bottom-left") << int(Qt::BottomEdge | Qt::LeftEdge)
                                     << QPoint(30, 40) << QRect(130, 100, 610, 520);
        QTest::newRow("bottom-right") << int(Qt::BottomEdge | Qt::RightEdge)
                                      << QPoint(30, 40) << QRect(100, 100, 670, 520);
        QTest::newRow("top-left-minimum") << int(Qt::TopEdge | Qt::LeftEdge)
                                          << QPoint(900, 900) << QRect(420, 340, 320, 240);
        QTest::newRow("top-left-maximum") << int(Qt::TopEdge | Qt::LeftEdge)
                                          << QPoint(-900, -900) << QRect(-260, -220, 1000, 800);
        QTest::newRow("bottom-right-minimum") << int(Qt::BottomEdge | Qt::RightEdge)
                                              << QPoint(-900, -900) << QRect(100, 100, 320, 240);
        QTest::newRow("bottom-right-maximum") << int(Qt::BottomEdge | Qt::RightEdge)
                                              << QPoint(900, 900) << QRect(100, 100, 1000, 800);
    }

    void edges()
    {
        QFETCH(int, edges);
        QFETCH(QPoint, delta);
        QFETCH(QRect, expected);
        QWindow window;
        prepare(window);
        WindowResizeHandler handler;
        const QPointF press(-500.25, 120.5);
        QVERIFY(handler.begin(&window, Qt::Edges(edges), press));
        mouseEvent(window, QEvent::MouseMove, press + delta);
        QCOMPARE(window.geometry(), expected);
        QVERIFY(handler.active());
        // Repeated input must not accumulate movement as the window origin changes.
        mouseEvent(window, QEvent::MouseMove, press + delta);
        QCOMPARE(window.geometry(), expected);
        mouseEvent(window, QEvent::MouseMove, press);
        QCOMPARE(window.geometry(), QRect(100, 100, 640, 480));
        mouseEvent(window, QEvent::MouseButtonRelease, press + delta, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(window.geometry(), expected);
        QVERIFY(!handler.active());
        mouseEvent(window, QEvent::MouseMove, press + QPointF(80, 80));
        QCOMPARE(window.geometry(), expected);
    }

    void cancellation_data()
    {
        QTest::addColumn<int>("eventType");
        QTest::newRow("ungrab") << int(QEvent::UngrabMouse);
        QTest::newRow("deactivate") << int(QEvent::WindowDeactivate);
        QTest::newRow("hide") << int(QEvent::Hide);
        QTest::newRow("state") << int(QEvent::WindowStateChange);
        QTest::newRow("close") << int(QEvent::Close);
    }

    void cancellation()
    {
        QFETCH(int, eventType);
        QWindow window;
        prepare(window);
        WindowResizeHandler handler;
        QSignalSpy changed(&handler, &WindowResizeHandler::activeChanged);
        QVERIFY(handler.begin(&window, Qt::TopEdge, QPointF(100, 100)));
        if (eventType == QEvent::WindowStateChange) {
            QWindowStateChangeEvent event(Qt::WindowNoState);
            QCoreApplication::sendEvent(&window, &event);
        } else {
            QEvent event{QEvent::Type(eventType)};
            QCoreApplication::sendEvent(&window, &event);
        }
        QVERIFY(!handler.active());
        handler.finish();
        QCOMPARE(changed.count(), 2);
        mouseEvent(window, QEvent::MouseMove, QPointF(100, 200));
        QCOMPARE(window.geometry(), QRect(100, 100, 640, 480));
    }

    void lifetimeAndInvalidInput()
    {
        auto window = std::make_unique<QWindow>();
        prepare(*window);
        WindowResizeHandler handler;
        QVERIFY(!handler.begin(nullptr, Qt::TopEdge, {}));
        QVERIFY(!handler.begin(window.get(), {}, {}));
        QVERIFY(!handler.begin(window.get(), Qt::TopEdge | Qt::BottomEdge, {}));
        QVERIFY(!handler.begin(window.get(), Qt::LeftEdge | Qt::RightEdge, {}));
        QVERIFY(!handler.begin(window.get(), Qt::Edges(0x10), {}));
        QVERIFY(handler.begin(window.get(), Qt::TopEdge, {}));
        window.reset();
        QVERIFY(!handler.active());
        QWindow second;
        prepare(second);
        QVERIFY(handler.begin(&second, Qt::BottomEdge, {}));
        mouseEvent(second, QEvent::MouseMove, QPointF(0, 20), Qt::NoButton, Qt::NoButton);
        QVERIFY(!handler.active());
        QCOMPARE(second.height(), 480);
        second.hide();
        QVERIFY(!handler.start(&second, Qt::BottomEdge, {}));
        second.showMaximized();
        QVERIFY(!handler.start(&second, Qt::BottomEdge, {}));
        second.showFullScreen();
        QVERIFY(!handler.start(&second, Qt::BottomEdge, {}));
    }

    void frameRouting()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import SubRec.Ui
Window {
    id: host
    width: 640
    height: 480
    flags: Qt.Window | Qt.FramelessWindowHint
    visible: true
    WindowResizeFrame {
        objectName: "frame"
        anchors.fill: parent
        window: host
    }
}
)", QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        auto *frame = root->findChild<QQuickItem *>(QStringLiteral("frame"));
        auto *handler = root->findChild<WindowResizeHandler *>();
        QVERIFY(window && frame && handler);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QCOMPARE(handler->property("usesManualResize").toBool(), WindowResizeHandler::usesManualResize());
        QCOMPARE(window->flags(), Qt::Window | Qt::FramelessWindowHint);
        QVERIFY(!frame->property("resizing").toBool());
#ifdef Q_OS_WIN
        // Exercise the real QML press -> Windows policy -> geometry -> release path.
        // No native system resize can run on the offscreen platform.
        const QList<QPoint> handles{{320, 2}, {320, 478}, {2, 240}, {638, 240},
                                    {2, 2}, {638, 2}, {2, 478}, {638, 478}};
        for (const QPoint &point : handles) {
            window->setGeometry(100, 100, 640, 480);
            QCoreApplication::processEvents();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point);
            QVERIFY(frame->property("resizing").toBool());
            QTest::mouseMove(window, point + QPoint(10, 10));
            QVERIFY(window->size() != QSize(640, 480));
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point);
            QVERIFY(!frame->property("resizing").toBool());
        }
#else
        // Execute the QML handler on Linux too; offscreen rejects system resizing.
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*(TypeError|ReferenceError).*")));
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, QPoint(320, 2));
        QVERIFY(!frame->property("resizing").toBool());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(320, 2));
#endif
        QVERIFY(handler->begin(window, Qt::TopEdge, {}));
        QVERIFY(frame->property("resizing").toBool());
        window->showMaximized();
        QTRY_VERIFY(!frame->isEnabled());
        QVERIFY(!frame->property("resizing").toBool());
        window->showFullScreen();
        QTRY_VERIFY(!frame->isEnabled());
    }
};

QTEST_MAIN(WindowResizeHandlerTests)
#include "window_resize_handler_tests.moc"

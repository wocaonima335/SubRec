#ifndef SUBREC_UI_FLOATING_BAR_VIEW_H
#define SUBREC_UI_FLOATING_BAR_VIEW_H

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QQuickWindow> // complete type: attachWindow() is Q_INVOKABLE
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace subrec::shell {
class FloatingBarPolicy;
}

namespace subrec::ui {

// Batch 2, module FB2. GUI thread. QML singleton `FloatingBarView`, the data and actions of
// FloatingBar.qml (MINIMIZE_DESIGN §3.6, §4.5, §6.3 FB-1…FB-6, FB-D, FB-H).
// Visibility policy is decided by shell::FloatingBarPolicy (FB1); this class only exposes it.
//
// Window contract: AppWindow creates exactly one FloatingBar (a QML Window, flags
// Qt.Tool | FramelessWindowHint | WindowStaysOnTopHint | WindowDoesNotAcceptFocus) with
// visible:false and calls attachWindow(bar) in Component.onCompleted. attachWindow forces
// the native window (winId) and hands it to CaptureExclusion BEFORE it is ever shown, so
// the first frame is already excluded (§4.4 item 4).
class FloatingBarView : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)
    // "preparing" (FB-1) | "running" (FB-2) | "paused" (FB-3) | "stopping" (FB-4) |
    // "saved" (FB-5) | "failed" (FB-6).
    Q_PROPERTY(QString barState READ barState NOTIFY barStateChanged)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY barStateChanged)   // "HH:MM:SS"
    Q_PROPERTY(QString captureError READ captureError NOTIFY barStateChanged) // FB-6 tooltip
    // "" (hidden) | "live" | "lagging" — the small captions icon.
    Q_PROPERTY(QString subtitleIndicator READ subtitleIndicator NOTIFY barStateChanged)
    Q_PROPERTY(bool hintVisible READ hintVisible NOTIFY hintVisibleChanged)      // FB-H

public:
    // Actions the bar triggers; main.cpp wires them to WindowController.
    struct Actions {
        std::function<void()> pauseResume;    // WindowController::requestPauseResume
        std::function<void()> stop;           // WindowController::requestStop
        std::function<void()> openMainWindow; // WindowController::showMainWindow
        std::function<void(QWindow *)> registerExcluded; // CaptureExclusion::addWindow
    };

    explicit FloatingBarView(QObject *parent); // no default: see singleton_provider.h
    ~FloatingBarView() override;

    static void setInstance(FloatingBarView *instance);
    static FloatingBarView *create(QQmlEngine *engine, QJSEngine *);

    // policy borrowed (may be null -> never shown). Elapsed/subtitle texts are pushed by
    // main.cpp from RecorderView/SubtitleView via setLiveTexts.
    void attach(shell::FloatingBarPolicy *policy, Actions actions);
    void setLiveTexts(const QString &elapsedText, const QString &captureError,
                      const QString &subtitleIndicator);

    bool shown() const;
    QString barState() const;
    QString elapsedText() const;
    QString captureError() const;
    QString subtitleIndicator() const;
    bool hintVisible() const;

    Q_INVOKABLE void attachWindow(QQuickWindow *bar);
    Q_INVOKABLE void pauseResume();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void openMainWindow();
    Q_INVOKABLE void dismiss();         // FB-6 close
    Q_INVOKABLE void acknowledgeHint(); // FB-H "知道了", 8 s timeout, or any bar button
    // Global top-left for a bar of the given size: remembered per screen, else top centre of
    // the recorded screen (camera: the main window's screen), 16 px from the top.
    Q_INVOKABLE QPoint preferredPosition(int barWidth, int barHeight) const;
    // Called on drag release with the snapped position (snap only within 24 px of an edge).
    Q_INVOKABLE void savePosition(int x, int y);

signals:
    void shownChanged();
    void barStateChanged();
    void hintVisibleChanged();

private:
    QPointer<shell::FloatingBarPolicy> policy_;
    Actions actions_;
    QPointer<QQuickWindow> bar_;
    std::unique_ptr<QTimer> hintTimer_; // FB-H: 8 s auto-dismiss
    QString elapsedText_;
    QString captureError_;
    QString subtitleIndicator_;
    int barWidthFallback() const;
};

} // namespace subrec::ui

#endif // SUBREC_UI_FLOATING_BAR_VIEW_H

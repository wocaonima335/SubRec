#ifndef SUBREC_UI_SUBTITLE_VIEW_H
#define SUBREC_UI_SUBTITLE_VIEW_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <functional>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace subrec::ui {

// Module U8. GUI thread. QML singleton `SubtitleView`: everything P1 (D panel subtitle
// section, control-bar SubtitleBadge), P2 (subtitle column) and P3-4 read about AI subtitles.
//
// The subtitle service does not exist in this branch yet. When it lands, main.cpp attaches
// the SubtitleService QObject (QML context name "subtitle", AI_SUBTITLE_DESIGN §5.7); property
// names forwarded below are the FROZEN ones from m0_headers/subtitle/service/
// subtitle_service_api.h (available/state/progress/lagSeconds/lastSrtPath/lastError,
// openLastSrtFolder). Unattached: state "off", available false — the UI shows U1 and
// recording is never blocked (§1.3 rule 5).
class SubtitleView : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // A SubtitleService is attached (main.cpp). False in this branch until the AI-subtitle
    // work lands: P1 hides its subtitle section and the control-bar badge, P3 hides the
    // "AI 字幕" sub-page (UI_BATCH_PLAN.md §6 decision 9). Not the frozen `available`.
    Q_PROPERTY(bool serviceAttached READ serviceAttached NOTIFY serviceChanged)

    // ---- forwarded (frozen names) ----
    Q_PROPERTY(bool available READ available NOTIFY serviceChanged)
    // "off" | "unavailable" | "idle" | "live" | "lagging" | "finishing" | "done" | "failed".
    Q_PROPERTY(QString state READ state NOTIFY serviceChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY serviceChanged)     // 0..1 (finishing)
    Q_PROPERTY(qreal lagSeconds READ lagSeconds NOTIFY serviceChanged)
    Q_PROPERTY(QString lastSrtPath READ lastSrtPath NOTIFY serviceChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY serviceChanged) // Chinese, user-facing

    // ---- asr/* settings (P3-4 table; defaults from AI_SUBTITLE_DESIGN §5.8) ----
    // "off" (first run) | "live" | "after". Locked while capturing or finishing.
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY settingsChanged)
    // mode != "off". Setting true restores the last non-off mode (default "live").
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString modelId READ modelId WRITE setModelId NOTIFY settingsChanged)   // default SenseVoice Small id
    Q_PROPERTY(int maxCharsPerLine READ maxCharsPerLine WRITE setMaxCharsPerLine NOTIFY settingsChanged) // 14|16|18|20|24, 18
    Q_PROPERTY(int threads READ threads WRITE setThreads NOTIFY settingsChanged)       // 1..4, 1
    Q_PROPERTY(bool srtBom READ srtBom WRITE setSrtBom NOTIFY settingsChanged)         // true
    Q_PROPERTY(bool keepSpool READ keepSpool WRITE setKeepSpool NOTIFY settingsChanged) // false
    Q_PROPERTY(QString modelDir READ modelDir NOTIFY settingsChanged)                  // read-only path

    // ---- derived ----
    // UI state code for §3.2.4 U1–U10: "off" | "needModel" | "downloading" | "unavailable" |
    // "ready" | "live" | "lagging" | "finishing" | "done" | "failed". Combines `state`,
    // ModelCatalog download status and `mode` (live vs after only changes texts).
    Q_PROPERTY(QString uiState READ uiState NOTIFY uiStateChanged)
    // True in live/lagging/finishing (and while capturing): settings marked 🔒 are locked.
    Q_PROPERTY(bool settingsLocked READ settingsLocked NOTIFY uiStateChanged)
    // R6: video the current/last job belongs to; "" if the service cannot tell.
    Q_PROPERTY(QString currentJobVideoPath READ currentJobVideoPath NOTIFY serviceChanged)

public:
    explicit SubtitleView(QObject *parent); // no default: see singleton_provider.h
    ~SubtitleView() override;

    static void setInstance(SubtitleView *instance);
    static SubtitleView *create(QQmlEngine *engine, QJSEngine *);

    // service: SubtitleService QObject (borrowed). reloadSettings: ISubtitleService::
    // reloadSettings(), called after any asr/* write. Either may be null/empty.
    void attach(QObject *service, std::function<void()> reloadSettings);

    bool serviceAttached() const;
    bool available() const;
    QString state() const;
    qreal progress() const;
    qreal lagSeconds() const;
    QString lastSrtPath() const;
    QString lastError() const;
    QString mode() const;
    void setMode(const QString &mode);
    bool enabled() const;
    void setEnabled(bool enabled);
    QString modelId() const;
    void setModelId(const QString &id);
    int maxCharsPerLine() const;
    void setMaxCharsPerLine(int chars);
    int threads() const;
    void setThreads(int threads);
    bool srtBom() const;
    void setSrtBom(bool bom);
    bool keepSpool() const;
    void setKeepSpool(bool keep);
    QString modelDir() const;
    QString uiState() const;
    bool settingsLocked() const;
    QString currentJobVideoPath() const;

    Q_INVOKABLE void openLastSrtFolder(); // forwarded (U9 action, T2)
    Q_INVOKABLE void openModelDir();      // P3-4 "打开"

signals:
    void serviceChanged();
    void settingsChanged();
    void uiStateChanged();
    // §3.2.4 rule 1: enabling with the selected model not installed -> AppWindow opens D2.
    void modelDownloadPromptRequested(const QString &modelId);
    // Edge-triggered from state changes: T2/T3 toasts, N3 when hidden.
    void subtitleDone(const QString &srtPath);
    void subtitleFailed(const QString &error);

private:
    QPointer<QObject> service_;
    std::function<void()> reloadSettings_;
};

} // namespace subrec::ui

#endif // SUBREC_UI_SUBTITLE_VIEW_H

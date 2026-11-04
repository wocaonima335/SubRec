#ifndef SUBREC_UI_AUDIO_LEVEL_MONITOR_H
#define SUBREC_UI_AUDIO_LEVEL_MONITOR_H

#include <QObject>
#include <QPointer>
#include <QElapsedTimer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace subrec::ui {

// Module B4. GUI thread. QML singleton `AudioLevelMonitor` feeding LevelMeter (§2.5.9).
//
// Source today: FFAudioSampler (recorder.audioSampler, recorder/ffaudiosampler.h), which
// reports ONE smoothed level of the recorded mix as volumeLevel 0..100 (= -40..0 dBFS) and
// only while recording. Hence:
// - `active` is false when idle: meters show an empty track and "开始录制后显示音量" (R5 fallback).
// - per-source levels are not available (perSourceAvailable=false); how P1 shows two meters
//   with one level is an open decision (UI_BATCH_PLAN.md §6, recommended: one shared meter).
// Updates are throttled to <= 30 Hz. Peak hold (1 s, then 20 dB/s) is done in LevelMeter.qml.
class AudioLevelMonitor : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    // dBFS, clamped to [-60, 0]; -60 when silent or inactive.
    Q_PROPERTY(qreal mixLevelDb READ mixLevelDb NOTIFY levelsChanged)
    Q_PROPERTY(bool perSourceAvailable READ perSourceAvailable CONSTANT)
    // Valid only when perSourceAvailable; otherwise equal to mixLevelDb.
    Q_PROPERTY(qreal systemLevelDb READ systemLevelDb NOTIFY levelsChanged)
    Q_PROPERTY(qreal microphoneLevelDb READ microphoneLevelDb NOTIFY levelsChanged)

public:
    explicit AudioLevelMonitor(QObject *parent); // no default: see singleton_provider.h
    ~AudioLevelMonitor() override;

    static void setInstance(AudioLevelMonitor *instance);
    static AudioLevelMonitor *create(QQmlEngine *engine, QJSEngine *);

    // FFAudioSampler as QObject* (volumeLevel, isActive). Re-attach when
    // RecorderView::audioSamplerChanged fires. Borrowed; nullptr detaches.
    void attach(QObject *audioSampler);

    bool active() const;
    qreal mixLevelDb() const;
    bool perSourceAvailable() const;
    qreal systemLevelDb() const;
    qreal microphoneLevelDb() const;

signals:
    void activeChanged();
    void levelsChanged();

private slots:
    void updateFromSampler();
private:
    QPointer<QObject> sampler_;
    qreal levelDb_ = -60.0;
    bool active_ = false;
    qint64 lastEmitMs_ = 0;
    QElapsedTimer throttle_;
    QTimer pending_;
};

} // namespace subrec::ui

#endif // SUBREC_UI_AUDIO_LEVEL_MONITOR_H

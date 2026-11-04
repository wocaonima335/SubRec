#include "ui/backend/audio_level_monitor.h"

#include "ui/backend/singleton_provider.h"
#include <QMetaProperty>

namespace subrec::ui {

namespace {
AudioLevelMonitor *s_instance = nullptr;
}

AudioLevelMonitor::AudioLevelMonitor(QObject *parent)
    : QObject(parent)
{
    pending_.setSingleShot(true);
    pending_.setTimerType(Qt::PreciseTimer);
    connect(&pending_, &QTimer::timeout, this, &AudioLevelMonitor::updateFromSampler);
}

AudioLevelMonitor::~AudioLevelMonitor()
{
    if (s_instance == this)
        s_instance = nullptr;
}

void AudioLevelMonitor::setInstance(AudioLevelMonitor *instance)
{
    s_instance = instance;
}

AudioLevelMonitor *AudioLevelMonitor::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(B4): connect volumeLevelChanged/activeStateChanged, convert 0..100 to dBFS, throttle.
void AudioLevelMonitor::attach(QObject *sampler)
{
    pending_.stop();
    throttle_.invalidate();
    if (sampler_)
        disconnect(sampler_, nullptr, this, nullptr);
    sampler_ = sampler;
    if (sampler_) {
        for (const auto *name : {"volumeLevel", "isActive"}) {
            const int index = sampler_->metaObject()->indexOfProperty(name);
            if (index < 0)
                continue;
            const auto property = sampler_->metaObject()->property(index);
            if (property.hasNotifySignal())
                connect(sampler_, property.notifySignal(), this,
                        metaObject()->method(metaObject()->indexOfSlot("updateFromSampler()")));
        }
        connect(sampler_, &QObject::destroyed, this, [this] { attach(nullptr); });
    }
    updateFromSampler();
}

void AudioLevelMonitor::updateFromSampler()
{
    if (throttle_.isValid() && throttle_.elapsed() < 34) {
        if (!pending_.isActive())
            pending_.start(34 - throttle_.elapsed());
        return;
    }
    throttle_.start();
    const bool activeValue = sampler_ && sampler_->property("isActive").toBool();
    const int volume = qBound(0, sampler_ ? sampler_->property("volumeLevel").toInt() : 0, 100);
    const qreal decibels = activeValue
        ? (volume <= 0 ? -60.0 : qMax(-60.0, -40.0 + 0.4 * volume)) : -60.0;
    if (activeValue != active_) {
        active_ = activeValue;
        emit activeChanged();
    }
    if (!qFuzzyCompare(decibels, levelDb_)) {
        levelDb_ = decibels;
        emit levelsChanged();
    }
}

bool AudioLevelMonitor::active() const
{
    return active_;
}
qreal AudioLevelMonitor::mixLevelDb() const
{
    return active_ ? levelDb_ : -60.0;
}
bool AudioLevelMonitor::perSourceAvailable() const
{
    return false;
}
qreal AudioLevelMonitor::systemLevelDb() const
{
    return mixLevelDb();
}
qreal AudioLevelMonitor::microphoneLevelDb() const
{
    return mixLevelDb();
}

} // namespace subrec::ui

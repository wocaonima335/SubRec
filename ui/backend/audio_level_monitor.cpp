#include "ui/backend/audio_level_monitor.h"

#include "ui/backend/singleton_provider.h"

namespace subrec::ui {

namespace {
AudioLevelMonitor *s_instance = nullptr;
}

AudioLevelMonitor::AudioLevelMonitor(QObject *parent) : QObject(parent) {}
AudioLevelMonitor::~AudioLevelMonitor()
{
    if (s_instance == this) s_instance = nullptr;
}

void AudioLevelMonitor::setInstance(AudioLevelMonitor *instance) { s_instance = instance; }
AudioLevelMonitor *AudioLevelMonitor::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(B4): connect volumeLevelChanged/activeStateChanged, convert 0..100 to dBFS, throttle.
void AudioLevelMonitor::attach(QObject *audioSampler) { sampler_ = audioSampler; }
bool AudioLevelMonitor::active() const { return false; }
qreal AudioLevelMonitor::mixLevelDb() const { return -60.0; }
bool AudioLevelMonitor::perSourceAvailable() const { return false; }
qreal AudioLevelMonitor::systemLevelDb() const { return -60.0; }
qreal AudioLevelMonitor::microphoneLevelDb() const { return -60.0; }

} // namespace subrec::ui

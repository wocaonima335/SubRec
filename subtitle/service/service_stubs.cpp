// M0 桩：SubtitleService 与 SubtitleJobRunner。真实实现归工作包 D；实现时拆成
// subtitle_service.cpp / subtitle_job_runner.cpp 并删除本文件。
// 桩行为：available=false、state="unavailable"；beginRecording 返回 nullptr（录制照常，不做字幕）。

#include "subtitle/service/subtitle_job_runner.h"
#include "subtitle/service/subtitle_service.h"

#include "subtitle/tap/audio_tap.h"

// ---- SubtitleService ----
SubtitleService::SubtitleService(QObject *parent) : QObject(parent) {}
SubtitleService::~SubtitleService() = default;

std::shared_ptr<subtitle::AudioTap> SubtitleService::beginRecording(const std::filesystem::path &) noexcept
{
    return nullptr;
}
void SubtitleService::endRecording(bool) noexcept {}

bool SubtitleService::available() const { return false; }
QString SubtitleService::state() const { return QStringLiteral("unavailable"); }
qreal SubtitleService::progress() const { return 0; }
qreal SubtitleService::lagSeconds() const { return 0; }
QString SubtitleService::lastSrtPath() const { return {}; }
QString SubtitleService::lastError() const { return {}; }
QString SubtitleService::currentJobVideoPath() const { return {}; }
QString SubtitleService::currentLine() const { return {}; }
QString SubtitleService::currentConfirmed() const { return {}; }
QString SubtitleService::currentTentative() const { return {}; }
int SubtitleService::degradeLevel() const { return 0; }
QString SubtitleService::degradeReason() const { return {}; }
QString SubtitleService::refineState() const { return {}; }
qreal SubtitleService::refineProgress() const { return 0; }

void SubtitleService::openLastSrtFolder() {}
bool SubtitleService::regenerate(const QString &) { return false; }
void SubtitleService::cancelCurrent() {}
void SubtitleService::restoreLive() {}
void SubtitleService::cancelRefine() {}
void SubtitleService::reloadSettings() {}
void SubtitleService::shutdown(int) {}

// ---- SubtitleJobRunner ----
SubtitleJobRunner::SubtitleJobRunner(AsrHostClientFactory *, QObject *parent) : QObject(parent) {}
SubtitleJobRunner::~SubtitleJobRunner() = default;

void SubtitleJobRunner::submit(quint64 jobId, QString, QString, subtitle::EngineRequest, subtitle::CueConfig, bool)
{
    emit jobFailed(jobId, static_cast<int>(subtitle::ErrorCode::NotSupported), QStringLiteral("M0 stub"));
}
void SubtitleJobRunner::notifyCommitted(quint64, qint64) {}
void SubtitleJobRunner::notifySplice(quint64, qint64) {}
void SubtitleJobRunner::finishJob(quint64, qint64, bool) {}
void SubtitleJobRunner::cancelJob(quint64) {}
void SubtitleJobRunner::shutdown(int) {}
void SubtitleJobRunner::onHealthSample(subtitle::RecorderHealth) {}
void SubtitleJobRunner::onPauseSplice(subtitle::JobId) {}
void SubtitleJobRunner::setLiveConfig(subtitle::LiveConfig) {}
void SubtitleJobRunner::restoreLive(subtitle::JobId) {}

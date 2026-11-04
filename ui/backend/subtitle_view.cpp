#include "ui/backend/subtitle_view.h"

#include "ui/backend/singleton_provider.h"

namespace subrec::ui {

namespace {
SubtitleView *s_instance = nullptr;
}

SubtitleView::SubtitleView(QObject *parent) : QObject(parent) {}
SubtitleView::~SubtitleView()
{
    if (s_instance == this) s_instance = nullptr;
}

void SubtitleView::setInstance(SubtitleView *instance) { s_instance = instance; }
SubtitleView *SubtitleView::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(U8): forward the frozen service properties; read/write asr/* through the subtitle
// settings store; derive uiState with ModelCatalog.
void SubtitleView::attach(QObject *service, std::function<void()> reloadSettings)
{
    service_ = service;
    reloadSettings_ = std::move(reloadSettings);
}

bool SubtitleView::serviceAttached() const { return !service_.isNull(); }
bool SubtitleView::available() const { return false; }
QString SubtitleView::state() const { return QStringLiteral("off"); }
qreal SubtitleView::progress() const { return 0.0; }
qreal SubtitleView::lagSeconds() const { return 0.0; }
QString SubtitleView::lastSrtPath() const { return {}; }
QString SubtitleView::lastError() const { return {}; }
QString SubtitleView::mode() const { return QStringLiteral("off"); }
void SubtitleView::setMode(const QString &) {}
bool SubtitleView::enabled() const { return false; }
void SubtitleView::setEnabled(bool) {}
QString SubtitleView::modelId() const { return {}; }
void SubtitleView::setModelId(const QString &) {}
int SubtitleView::maxCharsPerLine() const { return 18; }
void SubtitleView::setMaxCharsPerLine(int) {}
int SubtitleView::threads() const { return 1; }
void SubtitleView::setThreads(int) {}
bool SubtitleView::srtBom() const { return true; }
void SubtitleView::setSrtBom(bool) {}
bool SubtitleView::keepSpool() const { return false; }
void SubtitleView::setKeepSpool(bool) {}
QString SubtitleView::modelDir() const { return {}; }
QString SubtitleView::uiState() const { return QStringLiteral("off"); }
bool SubtitleView::settingsLocked() const { return false; }
QString SubtitleView::currentJobVideoPath() const { return {}; }
void SubtitleView::openLastSrtFolder() {}
void SubtitleView::openModelDir() {}

} // namespace subrec::ui

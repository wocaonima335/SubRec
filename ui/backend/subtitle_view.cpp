#include "ui/backend/subtitle_view.h"

#include "ui/backend/settings_store.h"
#include "ui/backend/singleton_provider.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>
#include <QMetaProperty>
#include "ui/backend/model_catalog.h"

namespace subrec::ui {

namespace {
SubtitleView *s_instance = nullptr;

QVariant objectProperty(const QPointer<QObject> &object, const char *name,
                  const QVariant &fallback = {})
{
    const auto value = object ? object->property(name) : QVariant();
    return value.isValid() ? value : fallback;
}
} // namespace

SubtitleView::SubtitleView(QObject *parent)
    : QObject(parent)
{
}

SubtitleView::~SubtitleView()
{
    if (s_instance == this)
        s_instance = nullptr;
}

void SubtitleView::setInstance(SubtitleView *instance)
{
    s_instance = instance;
}

SubtitleView *SubtitleView::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(U8): forward the frozen service properties; read/write asr/* through the subtitle
// settings store; derive uiState with ModelCatalog.
void SubtitleView::attach(QObject *service, std::function<void()> reloadSettings)
{
    if (service_)
        disconnect(service_, nullptr, this, nullptr);
    service_ = service;
    reloadSettings_ = std::move(reloadSettings);
    previousState_ = state();
    if (service_) {
        const auto slot = metaObject()->method(metaObject()->indexOfSlot("onServiceChanged()"));
        for (const char *name : {"available", "state", "progress", "lagSeconds", "lastSrtPath",
                                 "lastError", "modelDir", "currentJobVideoPath"}) {
            const int index = service_->metaObject()->indexOfProperty(name);
            if (index < 0)
                continue;
            const auto property = service_->metaObject()->property(index);
            if (property.hasNotifySignal())
                connect(service_, property.notifySignal(), this, slot, Qt::UniqueConnection);
        }
        connect(service_, &QObject::destroyed, this, [this] { attach(nullptr, {}); });
    }
    onServiceChanged();
}

void SubtitleView::onServiceChanged()
{
    const auto current = state();
    const bool edge = current != previousState_;
    previousState_ = current;
    emit serviceChanged();
    emit settingsChanged();
    emit uiStateChanged();
    if (edge && current == QStringLiteral("done"))
        emit subtitleDone(lastSrtPath());
    if (edge && current == QStringLiteral("failed"))
        emit subtitleFailed(lastError());
}

void SubtitleView::setModelCatalog(ModelCatalog *catalog)
{
    if (settingsLocked())
        return;
    if (catalog_)
        disconnect(catalog_, nullptr, this, nullptr);
    catalog_ = catalog;
    if (catalog_)
        connect(catalog_, &ModelCatalog::catalogChanged, this, &SubtitleView::uiStateChanged);
    emit uiStateChanged();
}

void SubtitleView::setCapturing(bool capturing)
{
    if (capturing_ == capturing)
        return;
    capturing_ = capturing;
    emit uiStateChanged();
}

bool SubtitleView::serviceAttached() const
{
    return !service_.isNull();
}
bool SubtitleView::available() const
{
    return serviceAttached() && objectProperty(service_, "available").toBool();
}
QString SubtitleView::state() const
{
    return objectProperty(service_, "state", QStringLiteral("off")).toString();
}
qreal SubtitleView::progress() const
{
    return objectProperty(service_, "progress", 0.0).toReal();
}
qreal SubtitleView::lagSeconds() const
{
    return objectProperty(service_, "lagSeconds", 0.0).toReal();
}
QString SubtitleView::lastSrtPath() const
{
    return objectProperty(service_, "lastSrtPath").toString();
}
QString SubtitleView::lastError() const
{
    return objectProperty(service_, "lastError").toString();
}

QString SubtitleView::mode() const
{
    return openUiSettings()->value(QStringLiteral("asr/mode"), QStringLiteral("off")).toString();
}

void SubtitleView::setMode(const QString &modeValue)
{
    if (settingsLocked())
        return;
    if (modeValue != QStringLiteral("off") && modeValue != QStringLiteral("live")
        && modeValue != QStringLiteral("after"))
        return;
    if (mode() == modeValue)
        return;
    openUiSettings()->setValue(QStringLiteral("asr/mode"), modeValue);
    if (modeValue != QStringLiteral("off"))
        openUiSettings()->setValue(QStringLiteral("asr/lastMode"), modeValue);
    if (modeValue != QStringLiteral("off") && catalog_ && !catalog_->selectedModelReady())
        emit modelDownloadPromptRequested(modelId());
    if (reloadSettings_)
        reloadSettings_();
    emit settingsChanged();
    emit uiStateChanged();
}

bool SubtitleView::enabled() const
{
    return mode() != QStringLiteral("off");
}
void SubtitleView::setEnabled(bool enabledValue)
{
    setMode(enabledValue ? openUiSettings()->value(QStringLiteral("asr/lastMode"),
        QStringLiteral("live")).toString() : QStringLiteral("off"));
}

QString SubtitleView::modelId() const
{
    return openUiSettings()->value(QStringLiteral("asr/modelId"), QStringLiteral("sensevoice-small")).toString();
}

void SubtitleView::setModelId(const QString &id)
{
    if (settingsLocked())
        return;
    if (modelId() == id)
        return;
    openUiSettings()->setValue(QStringLiteral("asr/modelId"), id);
    if (catalog_)
        catalog_->refreshSelection();
    emit uiStateChanged();
    if (reloadSettings_)
        reloadSettings_();
    emit settingsChanged();
}

int SubtitleView::maxCharsPerLine() const
{
    return qBound(14, openUiSettings()->value(QStringLiteral("asr/maxCharsPerLine"), 18).toInt(), 24);
}

void SubtitleView::setMaxCharsPerLine(int chars)
{
    if (settingsLocked())
        return;
    if (chars != 14 && chars != 16 && chars != 18 && chars != 20 && chars != 24)
        return;
    openUiSettings()->setValue(QStringLiteral("asr/maxCharsPerLine"), chars);
    if (reloadSettings_)
        reloadSettings_();
    emit settingsChanged();
}

int SubtitleView::threads() const
{
    return qBound(1, openUiSettings()->value(QStringLiteral("asr/threads"), 1).toInt(), 4);
}

void SubtitleView::setThreads(int value)
{
    if (settingsLocked())
        return;
    if (value < 1 || value > 4)
        return;
    openUiSettings()->setValue(QStringLiteral("asr/threads"), value);
    if (reloadSettings_)
        reloadSettings_();
    emit settingsChanged();
}

bool SubtitleView::srtBom() const
{
    return openUiSettings()->value(QStringLiteral("asr/srtBom"), true).toBool();
}
void SubtitleView::setSrtBom(bool value)
{
    if (settingsLocked())
        return;
    openUiSettings()->setValue(QStringLiteral("asr/srtBom"), value);
    if (reloadSettings_)
        reloadSettings_();
    emit settingsChanged();
}
bool SubtitleView::keepSpool() const
{
    return openUiSettings()->value(QStringLiteral("asr/keepSpool"), false).toBool();
}
void SubtitleView::setKeepSpool(bool value)
{
    if (settingsLocked())
        return;
    openUiSettings()->setValue(QStringLiteral("asr/keepSpool"), value);
    if (reloadSettings_)
        reloadSettings_();
    emit settingsChanged();
}
QString SubtitleView::modelDir() const
{
    return objectProperty(service_, "modelDir").toString();
}

QString SubtitleView::uiState() const
{
    if (!serviceAttached() || mode() == QStringLiteral("off"))
        return QStringLiteral("off");
    const auto serviceState = state();
    if (catalog_ && catalog_->anyDownloading())
        return QStringLiteral("downloading");
    if (catalog_ && !catalog_->selectedModelReady())
        return QStringLiteral("needModel");
    if (!available() || serviceState == QStringLiteral("unavailable"))
        return QStringLiteral("unavailable");
    if (serviceState == QStringLiteral("downloading"))
        return QStringLiteral("downloading");
    if (serviceState == QStringLiteral("live") || serviceState == QStringLiteral("lagging")
        || serviceState == QStringLiteral("finishing") || serviceState == QStringLiteral("done")
        || serviceState == QStringLiteral("failed"))
        return serviceState;
    if (serviceState == QStringLiteral("unavailable"))
        return QStringLiteral("unavailable");
    return QStringLiteral("ready");
}

bool SubtitleView::settingsLocked() const
{
    const auto stateValue = uiState();
    return capturing_ || stateValue == QStringLiteral("live") || stateValue == QStringLiteral("lagging")
        || stateValue == QStringLiteral("finishing");
}

QString SubtitleView::currentJobVideoPath() const
{
    return objectProperty(service_, "currentJobVideoPath").toString();
}

void SubtitleView::openLastSrtFolder()
{
    if (!lastSrtPath().isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(lastSrtPath()).absolutePath()));
}

void SubtitleView::openModelDir()
{
    if (!modelDir().isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(modelDir()));
}

} // namespace subrec::ui

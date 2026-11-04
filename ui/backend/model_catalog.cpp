#include "ui/backend/model_catalog.h"

#include "ui/backend/settings_store.h"
#include "ui/backend/singleton_provider.h"

#include <QMetaProperty>

namespace subrec::ui {

// ---- ModelListModel ----
// TODO(U8): rows from the built-in catalogue merged with ModelManager state.
ModelListModel::ModelListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    rows_ = {
        {{"modelId", "sensevoice-small"}, {"displayName", "SenseVoice Small"},
         {"tag", "recommended"}, {"summary", "中文、英文、日语、韩语、粤语 · 自带标点"},
         {"sizeBytes", qint64(0)}, {"sizeText", ""},
         {"licenseName", "待模型清单提供"}, {"licenseUrl", ""},
         {"status", "notInstalled"}, {"progress", 0.0}, {"selected", false}, {"inUse", false}},
        {{"modelId", "whisper-small"}, {"displayName", "Whisper Small"},
         {"tag", "alternative"}, {"summary", "多语言 · 中文可能出现繁体，标点不稳定"},
         {"sizeBytes", qint64(0)}, {"sizeText", ""},
         {"licenseName", "待模型清单提供"}, {"licenseUrl", ""},
         {"status", "notInstalled"}, {"progress", 0.0}, {"selected", false}, {"inUse", false}},
    };
    for (auto &row : rows_) {
        row[QStringLiteral("licenseText")] = QString();
        row[QStringLiteral("licenseAccepted")] = false;
        row[QStringLiteral("bytesDone")] = qint64(0);
        row[QStringLiteral("bytesTotal")] = qint64(0);
        row[QStringLiteral("errorText")] = QString();
    }
}

ModelListModel::~ModelListModel() = default;

int ModelListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : rows_.size();
}

QVariant ModelListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size())
        return {};
    const auto key = QString::fromLatin1(roleNames().value(role));
    return rows_.at(index.row()).value(key);
}

QHash<int, QByteArray> ModelListModel::roleNames() const
{
    return {
        {ModelIdRole, "modelId"}, {DisplayNameRole, "displayName"}, {TagRole, "tag"},
        {SummaryRole, "summary"}, {SizeBytesRole, "sizeBytes"}, {SizeTextRole, "sizeText"},
        {LicenseNameRole, "licenseName"}, {LicenseUrlRole, "licenseUrl"},
        {LicenseTextRole, "licenseText"}, {LicenseAcceptedRole, "licenseAccepted"},
        {StatusRole, "status"}, {ProgressRole, "progress"}, {BytesDoneRole, "bytesDone"},
        {BytesTotalRole, "bytesTotal"}, {ErrorTextRole, "errorText"},
        {SelectedRole, "selected"}, {InUseRole, "inUse"},
    };
}

int ModelListModel::count() const
{
    return rowCount();
}

// ---- ModelCatalog ----
namespace {
ModelCatalog *s_instance = nullptr;
}

ModelCatalog::ModelCatalog(QObject *parent)
    : QObject(parent), models_(new ModelListModel(this))
{
    refreshCatalog();
}

ModelCatalog::~ModelCatalog()
{
    if (s_instance == this)
        s_instance = nullptr;
}

void ModelCatalog::setInstance(ModelCatalog *instance)
{
    s_instance = instance;
}

ModelCatalog *ModelCatalog::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(U8): adapt ModelManager; licence bookkeeping; download state.
void ModelCatalog::attach(QObject *modelManager)
{
    if (manager_)
        disconnect(manager_, nullptr, this, nullptr);
    manager_ = modelManager;
    if (manager_) {
        const int index = manager_->metaObject()->indexOfProperty("models");
        if (index >= 0) {
            const auto property = manager_->metaObject()->property(index);
            if (property.hasNotifySignal())
                connect(manager_, property.notifySignal(), this,
                        metaObject()->method(metaObject()->indexOfSlot("refreshCatalog()")));
        }
        connect(manager_, &QObject::destroyed, this, [this] { attach(nullptr); });
    }
    refreshCatalog();
}

ModelListModel *ModelCatalog::models() const
{
    return models_;
}

bool ModelCatalog::selectedModelReady() const
{
    const auto settings = openUiSettings();
    const auto selected = settings->value(QStringLiteral("asr/modelId"),
                                          QStringLiteral("sensevoice-small")).toString();
    for (int row = 0; row < models_->rowCount(); ++row) {
        if (models_->data(models_->index(row), ModelListModel::ModelIdRole).toString() == selected)
            return models_->data(models_->index(row), ModelListModel::StatusRole).toString()
                == QStringLiteral("installed");
    }
    return false;
}

bool ModelCatalog::anyDownloading() const
{
    for (int row = 0; row < models_->rowCount(); ++row) {
        if (models_->data(models_->index(row), ModelListModel::StatusRole).toString()
            == QStringLiteral("downloading"))
            return true;
    }
    return false;
}

QVariantMap ModelCatalog::modelInfo(const QString &modelId) const
{
    for (int row = 0; row < models_->rowCount(); ++row) {
        if (models_->data(models_->index(row), ModelListModel::ModelIdRole).toString() != modelId)
            continue;
        QVariantMap result;
        const auto roles = models_->roleNames();
        for (auto iterator = roles.cbegin(); iterator != roles.cend(); ++iterator)
            result.insert(QString::fromLatin1(iterator.value()),
                          models_->data(models_->index(row), iterator.key()));
        result.insert(QStringLiteral("licenseVersion"), models_->rows_.at(row).value(QStringLiteral("licenseVersion")));
        return result;
    }
    return {};
}

void ModelCatalog::requestDownload(const QString &modelId)
{
    const auto info = modelInfo(modelId);
    if (!manager_ || info.isEmpty())
        return;
    if (!info.value(QStringLiteral("licenseAccepted")).toBool()) {
        emit licenseRequired(modelId);
        return;
    }
    acceptLicenseAndDownload(modelId);
}

void ModelCatalog::acceptLicenseAndDownload(const QString &modelId)
{
    if (!manager_ || modelInfo(modelId).isEmpty())
        return;
    const auto settings = openUiSettings();
    settings->setValue(QStringLiteral("asr/licenseAccepted/") + modelId, true);
    const auto version = modelInfo(modelId).value(QStringLiteral("licenseVersion")).toString();
    if (!version.isEmpty())
        settings->setValue(QStringLiteral("asr/licenseVersion/") + modelId, version);
    refreshCatalog();
    if (!QMetaObject::invokeMethod(manager_, "requestDownload", Q_ARG(QString, modelId)))
        emit downloadFailed(modelId, QStringLiteral("模型管理器未提供下载接口"));
}

void ModelCatalog::cancelDownload(const QString &modelId)
{
    if (manager_ && !modelInfo(modelId).isEmpty())
        QMetaObject::invokeMethod(manager_, "cancelDownload", Q_ARG(QString, modelId));
}

QString ModelCatalog::remove(const QString &modelId)
{
    if (!manager_)
        return QStringLiteral("模型服务尚未接入");
    const auto info = modelInfo(modelId);
    if (info.isEmpty())
        return QStringLiteral("模型不存在");
    if (info.value(QStringLiteral("inUse")).toBool())
        return QStringLiteral("模型正在使用");
    QString error;
    if (!QMetaObject::invokeMethod(manager_, "remove", Q_RETURN_ARG(QString, error), Q_ARG(QString, modelId)))
        return QStringLiteral("模型管理器未提供删除接口");
    refreshCatalog();
    return error;
}

void ModelCatalog::refreshSelection()
{
    refreshCatalog();
}

void ModelCatalog::refreshCatalog()
{
    const auto settings = openUiSettings();
    const auto supplied = manager_ ? manager_->property("models").toList() : QVariantList();
    for (int row = 0; row < models_->rows_.size(); ++row) {
        auto &info = models_->rows_[row];
        const QString id = info.value(QStringLiteral("modelId")).toString();
        const QString oldStatus = info.value(QStringLiteral("status")).toString();
        if (!manager_) {
            info[QStringLiteral("status")] = QStringLiteral("notInstalled");
            info[QStringLiteral("inUse")] = false;
        }
        for (const auto &entry : supplied) {
            const auto data = entry.toMap();
            if (data.value(QStringLiteral("modelId")).toString() == id) {
                for (auto it = data.cbegin(); it != data.cend(); ++it)
                    info[it.key()] = it.value();
            }
        }
        info[QStringLiteral("licenseAccepted")] = settings->value(
            QStringLiteral("asr/licenseAccepted/") + id, false).toBool();
        const auto version = info.value(QStringLiteral("licenseVersion")).toString();
        if (!version.isEmpty() && settings->value(QStringLiteral("asr/licenseVersion/") + id).toString() != version)
            info[QStringLiteral("licenseAccepted")] = false;
        info[QStringLiteral("selected")] = settings->value(QStringLiteral("asr/modelId"),
            QStringLiteral("sensevoice-small")).toString() == id;
        emit models_->dataChanged(models_->index(row), models_->index(row));
        const QString status = info.value(QStringLiteral("status")).toString();
        if (oldStatus != status && manager_) {
            if (status == QStringLiteral("installed")
                && (oldStatus == QStringLiteral("downloading") || oldStatus == QStringLiteral("verifying")))
                emit downloadFinished(id);
            else if (status == QStringLiteral("failed"))
                emit downloadFailed(id, info.value(QStringLiteral("errorText")).toString());
        }
    }
    emit catalogChanged();
}

} // namespace subrec::ui

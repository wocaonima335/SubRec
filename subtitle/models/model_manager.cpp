// M0 桩：ModelManager。真实实现归工作包 E（清单、下载、校验、许可确认、删除）。
// 桩行为：模型列表为空；下载立即失败。

#include "subtitle/models/model_manager.h"

ModelManager::ModelManager(QObject *parent) : QObject(parent) {}
ModelManager::~ModelManager() = default;

QVariantList ModelManager::models() const { return {}; }

void ModelManager::requestDownload(const QString &modelId)
{
    emit downloadFailed(modelId, QStringLiteral("M0 stub"));
}

void ModelManager::acceptLicense(const QString &, const QString &) {}
void ModelManager::cancelDownload(const QString &) {}
QString ModelManager::remove(const QString &) { return QStringLiteral("M0 stub"); }

std::optional<subtitle::EngineRequest> ModelManager::engineRequestFor(const QString &) const
{
    return std::nullopt;
}

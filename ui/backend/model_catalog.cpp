#include "ui/backend/model_catalog.h"

#include "ui/backend/singleton_provider.h"

namespace subrec::ui {

// ---- ModelListModel ----
// TODO(U8): rows from the built-in catalogue merged with ModelManager state.
ModelListModel::ModelListModel(QObject *parent) : QAbstractListModel(parent) {}
ModelListModel::~ModelListModel() = default;

int ModelListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 0;
}
QVariant ModelListModel::data(const QModelIndex &, int) const { return {}; }

QHash<int, QByteArray> ModelListModel::roleNames() const
{
    return {
        {ModelIdRole, "modelId"},
        {DisplayNameRole, "displayName"},
        {TagRole, "tag"},
        {SummaryRole, "summary"},
        {SizeBytesRole, "sizeBytes"},
        {SizeTextRole, "sizeText"},
        {LicenseNameRole, "licenseName"},
        {LicenseUrlRole, "licenseUrl"},
        {LicenseTextRole, "licenseText"},
        {LicenseAcceptedRole, "licenseAccepted"},
        {StatusRole, "status"},
        {ProgressRole, "progress"},
        {BytesDoneRole, "bytesDone"},
        {BytesTotalRole, "bytesTotal"},
        {ErrorTextRole, "errorText"},
        {SelectedRole, "selected"},
        {InUseRole, "inUse"},
    };
}

int ModelListModel::count() const { return rowCount(); }

// ---- ModelCatalog ----
namespace {
ModelCatalog *s_instance = nullptr;
}

ModelCatalog::ModelCatalog(QObject *parent) : QObject(parent), models_(new ModelListModel(this)) {}
ModelCatalog::~ModelCatalog()
{
    if (s_instance == this) s_instance = nullptr;
}

void ModelCatalog::setInstance(ModelCatalog *instance) { s_instance = instance; }
ModelCatalog *ModelCatalog::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(U8): adapt ModelManager; licence bookkeeping; download state.
void ModelCatalog::attach(QObject *modelManager) { manager_ = modelManager; }
ModelListModel *ModelCatalog::models() const { return models_; }
bool ModelCatalog::selectedModelReady() const { return false; }
bool ModelCatalog::anyDownloading() const { return false; }
QVariantMap ModelCatalog::modelInfo(const QString &) const { return {}; }
void ModelCatalog::requestDownload(const QString &) {}
void ModelCatalog::acceptLicenseAndDownload(const QString &) {}
void ModelCatalog::cancelDownload(const QString &) {}
QString ModelCatalog::remove(const QString &) { return {}; }

} // namespace subrec::ui

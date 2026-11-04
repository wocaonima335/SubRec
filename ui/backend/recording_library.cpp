#include "ui/backend/recording_library.h"

#include "ui/backend/singleton_provider.h"

namespace subrec::ui {

// ---- RecordingListModel ----
// TODO(U6): rows, filtering, sorting, limit.
RecordingListModel::RecordingListModel(QObject *parent) : QAbstractListModel(parent) {}
RecordingListModel::~RecordingListModel() = default;

int RecordingListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 0;
}
QVariant RecordingListModel::data(const QModelIndex &, int) const { return {}; }

QHash<int, QByteArray> RecordingListModel::roleNames() const
{
    return {
        {FileNameRole, "fileName"},
        {FilePathRole, "filePath"},
        {ModifiedRole, "modified"},
        {ModifiedTextRole, "modifiedText"},
        {SizeBytesRole, "sizeBytes"},
        {SizeTextRole, "sizeText"},
        {SubtitleStateRole, "subtitleState"},
        {SubtitleProgressRole, "subtitleProgress"},
        {SrtPathRole, "srtPath"},
        {IsRecordingRole, "isRecording"},
    };
}

int RecordingListModel::count() const { return rowCount(); }
QString RecordingListModel::filterText() const { return {}; }
void RecordingListModel::setFilterText(const QString &) {}
QString RecordingListModel::sortKey() const { return QStringLiteral("modified"); }
void RecordingListModel::setSortKey(const QString &) {}
bool RecordingListModel::sortDescending() const { return true; }
void RecordingListModel::setSortDescending(bool) {}
int RecordingListModel::limit() const { return 0; }
void RecordingListModel::setLimit(int) {}
QVariantMap RecordingListModel::get(int) const { return {}; }

// ---- RecordingLibrary ----
namespace {
RecordingLibrary *s_instance = nullptr;
}

RecordingLibrary::RecordingLibrary(QObject *parent)
    : QObject(parent), all_(new RecordingListModel(this)), recent_(new RecordingListModel(this))
{
}
RecordingLibrary::~RecordingLibrary()
{
    if (s_instance == this) s_instance = nullptr;
}

void RecordingLibrary::setInstance(RecordingLibrary *instance) { s_instance = instance; }
RecordingLibrary *RecordingLibrary::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(U6): own the two models, scan/watch the folder, trash files.
QString RecordingLibrary::folder() const { return {}; }
QString RecordingLibrary::folderStatus() const { return QStringLiteral("ok"); }
int RecordingLibrary::totalCount() const { return 0; }
qint64 RecordingLibrary::totalBytes() const { return 0; }
QString RecordingLibrary::summaryText() const { return {}; }
RecordingListModel *RecordingLibrary::all() const { return all_; }
RecordingListModel *RecordingLibrary::recent() const { return recent_; }
void RecordingLibrary::setFolder(const QString &) {}
void RecordingLibrary::setActiveRecording(const QString &) {}
void RecordingLibrary::setSubtitleJob(const QString &, const QString &, int) {}
void RecordingLibrary::refresh() {}
QString RecordingLibrary::moveToTrash(const QString &, bool) { return {}; }

} // namespace subrec::ui

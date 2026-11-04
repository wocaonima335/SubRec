#include "ui/backend/recording_library.h"

#include "ui/backend/singleton_provider.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <QFutureWatcher>
#include <QPromise>
#include <QThreadPool>
#include <QLocale>

namespace subrec::ui {

namespace {
QString modifiedText(const QDateTime &time)
{
    const auto date = time.date();
    if (date == QDate::currentDate())
        return QStringLiteral("今天 ") + time.toString(QStringLiteral("HH:mm"));
    if (date == QDate::currentDate().addDays(-1))
        return QStringLiteral("昨天 ") + time.toString(QStringLiteral("HH:mm"));
    return time.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}
}

// ---- RecordingListModel ----
// TODO(U6): rows, filtering, sorting, limit.
RecordingListModel::RecordingListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

RecordingListModel::~RecordingListModel() = default;

int RecordingListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return visibleRows_.size();
}

QVariant RecordingListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= visibleRows_.size())
        return {};
    const auto &row = visibleRows_.at(index.row());
    switch (role) {
    case FileNameRole: return row.fileName;
    case FilePathRole: return row.filePath;
    case ModifiedRole: return row.modified;
    case ModifiedTextRole: return row.modifiedText;
    case SizeBytesRole: return row.sizeBytes;
    case SizeTextRole: return row.sizeBytes < 0 ? QStringLiteral("—") : QLocale().formattedDataSize(row.sizeBytes);
    case SubtitleStateRole: return row.isRecording ? QStringLiteral("recording") : row.subtitleState;
    case SubtitleProgressRole: return row.subtitleProgress;
    case SrtPathRole: return row.srtPath;
    case IsRecordingRole: return row.isRecording;
    default: return {};
    }
}

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

int RecordingListModel::count() const
{
    return rowCount();
}

QString RecordingListModel::filterText() const
{
    return filter_;
}

void RecordingListModel::setFilterText(const QString &text)
{
    if (filter_ == text)
        return;
    filter_ = text;
    emit filterChanged();
    reset();
}

QString RecordingListModel::sortKey() const
{
    return sortKey_;
}

void RecordingListModel::setSortKey(const QString &key)
{
    if (key != QStringLiteral("name") && key != QStringLiteral("modified")
        && key != QStringLiteral("size"))
        return;
    if (sortKey_ == key)
        return;
    sortKey_ = key;
    emit sortChanged();
    reset();
}

bool RecordingListModel::sortDescending() const
{
    return descending_;
}

void RecordingListModel::setSortDescending(bool descending)
{
    if (descending_ == descending)
        return;
    descending_ = descending;
    emit sortChanged();
    reset();
}

int RecordingListModel::limit() const
{
    return limit_;
}

void RecordingListModel::setLimit(int limit)
{
    limit = qMax(0, limit);
    if (limit_ == limit)
        return;
    limit_ = limit;
    emit filterChanged();
    reset();
}

void RecordingListModel::reset()
{
    beginResetModel();
    visibleRows_.clear();
    if (const auto *library = qobject_cast<RecordingLibrary *>(parent())) {
        for (auto row : library->rows_) {
            if (!row.fileName.contains(filter_, Qt::CaseInsensitive))
                continue;
            row.isRecording = row.filePath == library->activePath_;
            if (row.isRecording)
                row.sizeBytes = -1;
            if (row.filePath == library->subtitlePath_) {
                row.subtitleState = library->subtitleState_;
                row.subtitleProgress = library->subtitleProgress_;
            }
            visibleRows_.push_back(row);
        }
    }
    std::stable_sort(visibleRows_.begin(), visibleRows_.end(), [this](const auto &a, const auto &b) {
        int order = QString::compare(a.fileName, b.fileName, Qt::CaseInsensitive);
        if (sortKey_ == QStringLiteral("size") && a.sizeBytes != b.sizeBytes)
            order = a.sizeBytes < b.sizeBytes ? -1 : 1;
        if (sortKey_ == QStringLiteral("modified") && a.modified != b.modified)
            order = a.modified < b.modified ? -1 : 1;
        return descending_ ? order > 0 : order < 0;
    });
    if (limit_ > 0 && visibleRows_.size() > limit_)
        visibleRows_.resize(limit_);
    endResetModel();
    emit countChanged();
}

QVariantMap RecordingListModel::get(int row) const
{
    if (row < 0 || row >= count())
        return {};
    QVariantMap result;
    const auto roles = roleNames();
    for (auto iterator = roles.cbegin(); iterator != roles.cend(); ++iterator)
        result.insert(QString::fromLatin1(iterator.value()), data(index(row, 0), iterator.key()));
    return result;
}

// ---- RecordingLibrary ----
namespace {
RecordingLibrary *s_instance = nullptr;
}

RecordingLibrary::RecordingLibrary(QObject *parent)
    : QObject(parent), all_(new RecordingListModel(this)), recent_(new RecordingListModel(this))
{
    recent_->setLimit(3);
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(300);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this] {
        refreshTimer_.start();
    });
    connect(&refreshTimer_, &QTimer::timeout, this, &RecordingLibrary::refresh);
}

RecordingLibrary::~RecordingLibrary()
{
    if (s_instance == this)
        s_instance = nullptr;
}

void RecordingLibrary::setInstance(RecordingLibrary *instance)
{
    s_instance = instance;
}

RecordingLibrary *RecordingLibrary::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(U6): own the two models, scan/watch the folder, trash files.
QString RecordingLibrary::folder() const
{
    return folder_;
}

QString RecordingLibrary::folderStatus() const
{
    const QFileInfo info(folder_);
    if (folder_.isEmpty() || !info.isDir())
        return QStringLiteral("missing");
    if (!info.exists())
        return QStringLiteral("missing");
    return info.isReadable() ? QStringLiteral("ok") : QStringLiteral("noAccess");
}

int RecordingLibrary::totalCount() const
{
    return rows_.size();
}

qint64 RecordingLibrary::totalBytes() const
{
    qint64 total = 0;
    for (const auto &row : rows_) {
        if (row.sizeBytes > 0)
            total += row.sizeBytes;
    }
    return total;
}

QString RecordingLibrary::summaryText() const
{
    return QStringLiteral("%1 · 共 %2 个录像 · %3").arg(folder_).arg(totalCount())
        .arg(QLocale().formattedDataSize(totalBytes()));
}

RecordingListModel *RecordingLibrary::all() const
{
    return all_;
}

RecordingListModel *RecordingLibrary::recent() const
{
    return recent_;
}

void RecordingLibrary::setFolder(const QString &folder)
{
    const auto absolute = folder.isEmpty() ? QString() : QDir(folder).absolutePath();
    if (folder_ == absolute)
        return;
    if (!watcher_.directories().isEmpty())
        watcher_.removePaths(watcher_.directories());
    folder_ = absolute;
    if (QFileInfo(folder_).isDir())
        watcher_.addPath(folder_);
    emit folderChanged();
    refresh();
}

void RecordingLibrary::setActiveRecording(const QString &path)
{
    activePath_ = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
    rebuild();
}

void RecordingLibrary::setSubtitleJob(const QString &path, const QString &state, int progress)
{
    subtitlePath_ = path;
    subtitleState_ = state;
    subtitleProgress_ = qBound(0, progress, 100);
    rebuild();
}

void RecordingLibrary::rebuild()
{
    all_->reset();
    recent_->reset();
    emit contentsChanged();
}

void RecordingLibrary::refresh()
{
    const auto generation = ++scanGeneration_;
    const QString folder = folder_;
    auto *watcher = new QFutureWatcher<QVector<RecordingRow>>(this);
    connect(watcher, &QFutureWatcher<QVector<RecordingRow>>::finished, this,
            [this, watcher, generation] {
        if (generation == scanGeneration_) {
            rows_ = watcher->result();
            rebuild();
            emit folderChanged();
        }
        watcher->deleteLater();
    });
    auto promise = std::make_shared<QPromise<QVector<RecordingRow>>>();
    promise->start();
    watcher->setFuture(promise->future());
    QThreadPool::globalInstance()->start([folder, promise] {
        QVector<RecordingRow> rows;
        if (!folder.isEmpty()) {
            const QDir directory(folder);
            const auto files = directory.entryInfoList(
                {QStringLiteral("*.mp4"), QStringLiteral("*.avi"), QStringLiteral("*.mkv"),
                 QStringLiteral("*.flv"), QStringLiteral("*.mov")}, QDir::Files, QDir::NoSort);
            for (const auto &info : files) {
                RecordingRow row;
                row.fileName = info.fileName();
                row.filePath = info.absoluteFilePath();
                row.modified = info.lastModified();
                row.modifiedText = modifiedText(row.modified);
                row.sizeBytes = info.size();
                const QFileInfo subtitle(info.path() + QDir::separator()
                                         + info.completeBaseName() + QStringLiteral(".srt"));
                row.srtPath = subtitle.exists() ? subtitle.absoluteFilePath() : QString();
                row.subtitleState = row.srtPath.isEmpty() ? QStringLiteral("none") : QStringLiteral("has");
                rows.push_back(row);
            }
        }
        promise->addResult(rows);
        promise->finish();
    });
}

QString RecordingLibrary::moveToTrash(const QString &path, bool alsoSubtitle)
{
    if (path.isEmpty() || QFileInfo(path).absoluteFilePath() == activePath_)
        return QStringLiteral("录制中的文件不能删除");
    const auto absolute = QFileInfo(path).absoluteFilePath();
    const bool owned = std::any_of(rows_.cbegin(), rows_.cend(), [&absolute](const auto &row) {
        return row.filePath == absolute;
    });
    if (!owned)
        return QStringLiteral("无法删除：文件不在录像库中");
    if (!QFile::moveToTrash(path))
        return QStringLiteral("无法删除：文件不存在或权限不足");
    if (alsoSubtitle) {
        const QFileInfo video(path);
        const auto subtitle = video.path() + QDir::separator() + video.completeBaseName() + ".srt";
        if (QFileInfo::exists(subtitle) && !QFile::moveToTrash(subtitle)) {
            refresh();
            return QStringLiteral("无法删除字幕：文件权限不足");
        }
    }
    refresh();
    return {};
}

} // namespace subrec::ui

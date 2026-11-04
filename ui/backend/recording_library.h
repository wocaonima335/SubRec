#ifndef SUBREC_UI_RECORDING_LIBRARY_H
#define SUBREC_UI_RECORDING_LIBRARY_H

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace subrec::ui {

// Module U6. One row per video file in the output folder (UI_REDESIGN §3.3).
// Two instances live in RecordingLibrary: `all` (P2 table) and `recent` (P1 E area,
// limit 3, always newest first). Filtering/sorting happen here, not in QML.
class RecordingListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by RecordingLibrary")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // Case-insensitive substring on fileName (P2 search box, L4 when it filters to 0).
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
    // "name" | "modified" (default) | "size".
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY sortChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY sortChanged)
    // 0 = unlimited; `recent` uses 3.
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY filterChanged)

public:
    enum Role {
        FileNameRole = Qt::UserRole + 1, // "SubRec_20261101_101500.mp4"
        FilePathRole,       // absolute path
        ModifiedRole,       // QDateTime (file mtime = "录制时间")
        ModifiedTextRole,   // "今天 10:15" / "昨天 21:03" / "2026-10-28 09:40"
        SizeBytesRole,      // qint64; -1 while being written
        SizeTextRole,       // "245 MB" / "1.2 GB"; "—" while being written
        SubtitleStateRole,  // "none" | "has" | "generating" | "failed" | "recording" (§3.3.1, L5)
        SubtitleProgressRole, // 0..100 when "generating"
        SrtPathRole,        // same-name .srt absolute path or ""
        IsRecordingRole,    // bool: this is RecorderView.currentOutputFile (L5: no play/delete)
    };
    Q_ENUM(Role)

    explicit RecordingListModel(QObject *parent = nullptr);
    ~RecordingListModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    QString filterText() const;
    void setFilterText(const QString &text);
    QString sortKey() const;
    void setSortKey(const QString &key);
    bool sortDescending() const;
    void setSortDescending(bool descending);
    int limit() const;
    void setLimit(int limit);

    // One row as a map keyed by roleNames() ("fileName", "filePath", ...); {} if out of range.
    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void countChanged();
    void filterChanged();
    void sortChanged();
};

// Module U6. GUI thread. QML singleton `RecordingLibrary`.
// Scans `folder` for *.mp4 *.avi *.mkv *.flv *.mov (same filter as the old cuttingpage.qml),
// pairs same-name .srt files, watches the folder (QFileSystemWatcher, coalesced >= 300 ms)
// and rescans on refresh(). Scanning must not block the GUI thread for large folders
// (decision in UI_BATCH_PLAN.md §6; recommended: scan on a worker, publish on GUI thread).
class RecordingLibrary : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // Follows OutputNaming.outputDir (set by main.cpp/attach, not by QML).
    Q_PROPERTY(QString folder READ folder NOTIFY folderChanged)
    // "ok" | "missing" | "noAccess" (L3).
    Q_PROPERTY(QString folderStatus READ folderStatus NOTIFY folderChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY contentsChanged)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY contentsChanged)
    // "D:\Videos\SubRec · 共 12 个录像 · 3.1 GB" (P2 path & stats line).
    Q_PROPERTY(QString summaryText READ summaryText NOTIFY contentsChanged)
    Q_PROPERTY(subrec::ui::RecordingListModel *all READ all CONSTANT)
    Q_PROPERTY(subrec::ui::RecordingListModel *recent READ recent CONSTANT)

public:
    explicit RecordingLibrary(QObject *parent); // no default: see singleton_provider.h
    ~RecordingLibrary() override;

    static void setInstance(RecordingLibrary *instance);
    static RecordingLibrary *create(QQmlEngine *engine, QJSEngine *);

    QString folder() const;
    QString folderStatus() const;
    int totalCount() const;
    qint64 totalBytes() const;
    QString summaryText() const;
    RecordingListModel *all() const;
    RecordingListModel *recent() const;

    // C++ wiring (main.cpp / RecorderView / SubtitleView signals). GUI thread.
    void setFolder(const QString &folder);
    void setActiveRecording(const QString &absolutePath);   // "" when not capturing (L5)
    // R6: the video the current subtitle job belongs to. state as SubtitleStateRole values.
    void setSubtitleJob(const QString &videoPath, const QString &state, int progressPercent);

    Q_INVOKABLE void refresh();
    // D5. Moves the video (and its .srt when alsoSubtitle) to the recycle bin with
    // QFile::moveToTrash (R10). Returns "" on success, else "无法删除：{原因}" for T3.
    // Refuses the active recording.
    Q_INVOKABLE QString moveToTrash(const QString &absolutePath, bool alsoSubtitle);

signals:
    void folderChanged();
    void contentsChanged();

private:
    RecordingListModel *all_ = nullptr;    // child, never null
    RecordingListModel *recent_ = nullptr; // child, never null
};

} // namespace subrec::ui

#endif // SUBREC_UI_RECORDING_LIBRARY_H

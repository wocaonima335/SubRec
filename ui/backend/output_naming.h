#ifndef SUBREC_UI_OUTPUT_NAMING_H
#define SUBREC_UI_OUTPUT_NAMING_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace subrec::ui {

// Module B3. GUI thread. QML singleton `OutputNaming`. Owns R1 (file-name template, a new
// file per recording, _2/_3 on collision) and R2 (folder and template persisted).
//
// Engine contract (no recorder/** change): FFRecorder keeps one outputPath/outputFileName
// pair. At startup attach() pushes the persisted folder into recorder.outputPath. Before
// every start, main.cpp's RecordingActions start callback (engineStart) calls
// prepareNextFile(), which resolves the next unique name and writes recorder.outputFileName,
// THEN starts the scheduler. So the START INVARIANT is untouched: still exactly one path
// (WindowController::requestStart -> actions.start).
// AutoTest keeps SUBREC_AUTOTEST_OUTPUT: main.cpp skips prepareNextFile() when it is set.
class OutputNaming : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // output/dir. Default QStandardPaths::MoviesLocation. Setting it writes recorder.outputPath
    // (ignored while capturing; the P3-2 field is locked then).
    Q_PROPERTY(QString outputDir READ outputDir WRITE setOutputDir NOTIFY outputDirChanged)
    // "" | "文件夹不存在或没有写入权限". Re-evaluated on set and on refreshDirStatus().
    Q_PROPERTY(QString outputDirError READ outputDirError NOTIFY outputDirChanged)
    // output/fileNameTemplate, default naming::defaultTemplate(). An invalid write is NOT
    // stored: templateError is set and the last valid template stays in effect.
    Q_PROPERTY(QString fileNameTemplate READ fileNameTemplate WRITE setFileNameTemplate NOTIFY templateChanged)
    Q_PROPERTY(QString templateError READ templateError NOTIFY templateChanged)
    // Example for P3-2 / P1 save area: expansion at "now" + ".mp4", e.g. SubRec_20261101_101500.mp4.
    // Refreshed when the template changes and at least once per minute while visible
    // (QML may call refreshPreview()).
    Q_PROPERTY(QString previewFileName READ previewFileName NOTIFY previewChanged)

public:
    explicit OutputNaming(QObject *parent); // no default: see singleton_provider.h
    ~OutputNaming() override;

    static void setInstance(OutputNaming *instance);
    static OutputNaming *create(QQmlEngine *engine, QJSEngine *);

    // FFRecorder as QObject* (outputPath / outputFileName properties). Borrowed.
    void attach(QObject *recorder);

    QString outputDir() const;
    void setOutputDir(const QString &dir);
    QString outputDirError() const;
    QString fileNameTemplate() const;
    void setFileNameTemplate(const QString &tmpl);
    QString templateError() const;
    QString previewFileName() const;

    Q_INVOKABLE QString validateTemplate(const QString &tmpl) const; // naming::validateTemplate
    Q_INVOKABLE void refreshPreview();
    Q_INVOKABLE void refreshDirStatus();

    // C++ only, GUI thread, called from the start action right before captureScheduler().start().
    // sourceLabel: "屏幕" | "摄像头". Returns the absolute path that will be written; also
    // stores it in recorder.outputFileName. Creates the folder if missing (mkpath); on failure
    // returns "" and leaves the recorder untouched so the engine reports its usual error.
    QString prepareNextFile(const QString &sourceLabel);

signals:
    void outputDirChanged();
    void templateChanged();
    void previewChanged();

private:
    QPointer<QObject> recorder_;
};

} // namespace subrec::ui

#endif // SUBREC_UI_OUTPUT_NAMING_H

#include "ui/backend/output_naming.h"

#include "ui/backend/output_naming_logic.h"
#include "ui/backend/settings_store.h"
#include "ui/backend/singleton_provider.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace subrec::ui {

namespace {
OutputNaming *s_instance = nullptr;
}

OutputNaming::OutputNaming(QObject *parent)
    : QObject(parent)
{
}

OutputNaming::~OutputNaming()
{
    if (s_instance == this)
        s_instance = nullptr;
}

void OutputNaming::setInstance(OutputNaming *instance)
{
    s_instance = instance;
}

OutputNaming *OutputNaming::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(B3): persistence via openUiSettings(), push folder to recorder, unique names.
void OutputNaming::attach(QObject *recorder)
{
    recorder_ = recorder;
    if (recorder_ && !qEnvironmentVariableIsSet("SUBREC_AUTOTEST_OUTPUT"))
        recorder_->setProperty("outputPath", outputDir());
}

QString OutputNaming::outputDir() const
{
    const auto settings = openUiSettings();
    return settings->value(QStringLiteral("output/dir"),
                           QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).toString();
}

void OutputNaming::setOutputDir(const QString &directory)
{
    if (capturing() || directory.trimmed().isEmpty())
        return;
    const auto absolute = QDir(directory).absolutePath();
    if (absolute == outputDir())
        return;
    const auto settings = openUiSettings();
    settings->setValue(QStringLiteral("output/dir"), absolute);
    if (recorder_)
        recorder_->setProperty("outputPath", absolute);
    emit outputDirChanged();
}

QString OutputNaming::outputDirError() const
{
    const QFileInfo info(outputDir());
    if (!info.exists())
        return QStringLiteral("文件夹不存在或没有写入权限");
    return info.isDir() && info.isWritable() ? QString() : QStringLiteral("文件夹不存在或没有写入权限");
}

QString OutputNaming::fileNameTemplate() const
{
    const auto settings = openUiSettings();
    return settings->value(QStringLiteral("output/fileNameTemplate"), naming::defaultTemplate()).toString();
}

void OutputNaming::setFileNameTemplate(const QString &templateText)
{
    if (capturing())
        return;
    const auto error = naming::validateTemplate(templateText);
    templateError_ = error;
    if (!error.isEmpty()) {
        emit templateChanged();
        return;
    }
    if (fileNameTemplate() == templateText) {
        emit templateChanged();
        return;
    }
    const auto settings = openUiSettings();
    settings->setValue(QStringLiteral("output/fileNameTemplate"), templateText);
    emit templateChanged();
    emit previewChanged();
}

QString OutputNaming::templateError() const
{
    return templateError_;
}

QString OutputNaming::previewFileName() const
{
    return naming::expandTemplate(fileNameTemplate(), QDateTime::currentDateTime(),
                                  QStringLiteral("屏幕")) + QStringLiteral(".mp4");
}

QString OutputNaming::validateTemplate(const QString &templateText) const
{
    return naming::validateTemplate(templateText);
}

void OutputNaming::refreshPreview()
{
    emit previewChanged();
}

void OutputNaming::refreshDirStatus()
{
    emit outputDirChanged();
}

QString OutputNaming::prepareNextFile(const QString &sourceLabel)
{
    if (!recorder_ || capturing())
        return {};
    if (qEnvironmentVariableIsSet("SUBREC_AUTOTEST_OUTPUT"))
        return QDir(recorder_->property("outputPath").toString()).absoluteFilePath(
            recorder_->property("outputFileName").toString());
    const auto directory = outputDir();
    if (!QDir().mkpath(directory) || !outputDirError().isEmpty())
        return {};
    const auto base = naming::expandTemplate(fileNameTemplate(), QDateTime::currentDateTime(), sourceLabel);
    const auto exists = [](const QString &path) {
        const QFileInfo video(path);
        const auto subtitle = video.path() + QDir::separator() + video.completeBaseName() + ".srt";
        return QFileInfo::exists(path) || QFileInfo::exists(subtitle);
    };
    const auto name = naming::uniqueFileName(directory, base, ".mp4", exists);
    if (name.isEmpty())
        return {};
    recorder_->setProperty("outputPath", directory);
    recorder_->setProperty("outputFileName", name);
    return QDir(directory).filePath(name);
}

bool OutputNaming::capturing() const
{
    const auto state = recorder_ ? recorder_->property("captureState").toString() : QString();
    return state == "Preparing" || state == "Running" || state == "Paused" || state == "Stopping";
}

void OutputNaming::setOutputFolderUrl(const QUrl &url)
{
    if (url.isLocalFile())
        setOutputDir(url.toLocalFile());
}

} // namespace subrec::ui

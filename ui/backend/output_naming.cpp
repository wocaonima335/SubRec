#include "ui/backend/output_naming.h"

#include "ui/backend/output_naming_logic.h"
#include "ui/backend/singleton_provider.h"

namespace subrec::ui {

namespace {
OutputNaming *s_instance = nullptr;
}

OutputNaming::OutputNaming(QObject *parent) : QObject(parent) {}
OutputNaming::~OutputNaming()
{
    if (s_instance == this) s_instance = nullptr;
}

void OutputNaming::setInstance(OutputNaming *instance) { s_instance = instance; }
OutputNaming *OutputNaming::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(B3): persistence via openUiSettings(), push folder to recorder, unique names.
void OutputNaming::attach(QObject *recorder) { recorder_ = recorder; }
QString OutputNaming::outputDir() const { return {}; }
void OutputNaming::setOutputDir(const QString &) {}
QString OutputNaming::outputDirError() const { return {}; }
QString OutputNaming::fileNameTemplate() const { return naming::defaultTemplate(); }
void OutputNaming::setFileNameTemplate(const QString &) {}
QString OutputNaming::templateError() const { return {}; }
QString OutputNaming::previewFileName() const { return QStringLiteral("SubRec_20261101_101500.mp4"); }
QString OutputNaming::validateTemplate(const QString &tmpl) const { return naming::validateTemplate(tmpl); }
void OutputNaming::refreshPreview() {}
void OutputNaming::refreshDirStatus() {}
QString OutputNaming::prepareNextFile(const QString &) { return {}; }

} // namespace subrec::ui

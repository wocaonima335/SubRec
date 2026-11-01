#include "shell/shell_state_source.h"
#include "shell/shell_logic.h"
#ifdef SUBREC_SHELL_CONTROLLER_TEST_DOUBLES
#include "fakes/observable_recorder.h"
#else
#include "recorder/ffrecorder.h"
#endif

#include <QDir>
#include <QVariantMap>

namespace subrec::shell {

class ShellStateSource::Private {
public:
    explicit Private(FFRecorder &recorder) : recorder(recorder) {}
    FFRecorder &recorder;
};

ShellStateSource::ShellStateSource(FFRecorder &recorder, QObject *parent)
    : QObject(parent), d(std::make_unique<Private>(recorder))
{
    connect(&recorder, &FFRecorder::captureStateChanged, this, &ShellStateSource::changed);
    connect(&recorder, &FFRecorder::captureTimeTextChanged, this, &ShellStateSource::changed);
    connect(&recorder, &FFRecorder::captureErrorChanged, this, &ShellStateSource::changed);
    connect(&recorder, &FFRecorder::captureDevicesChanged, this, &ShellStateSource::changed);
    connect(&recorder, &FFRecorder::outputPathChanged, this, &ShellStateSource::changed);
    connect(&recorder, &FFRecorder::outputFileNameChanged, this, &ShellStateSource::changed);
}

ShellStateSource::~ShellStateSource() = default;

ShellSnapshot ShellStateSource::snapshot() const
{
    ShellSnapshot result;
    bool hasVideo = false;
    for (const auto &entry : d->recorder.captureDevices()) {
        const auto device = entry.toMap();
        const auto kind = device.value(QStringLiteral("kind")).toString();
        if ((kind == QLatin1String("screen") || kind == QLatin1String("camera"))
            && device.value(QStringLiteral("available")).toBool()) {
            hasVideo = true;
            break;
        }
    }
    result.state = recStateFrom(d->recorder.captureState(), hasVideo);
    result.timeText = d->recorder.captureTimeText();
    result.captureError = d->recorder.captureError();
    if (!d->recorder.outputFileName().isEmpty())
        result.outputFile = QDir(d->recorder.outputPath()).filePath(d->recorder.outputFileName());
    return result;
}

} // namespace subrec::shell

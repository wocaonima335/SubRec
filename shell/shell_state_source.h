#ifndef SUBREC_SHELL_STATE_SOURCE_H
#define SUBREC_SHELL_STATE_SOURCE_H

#include "shell_types.h"

#include <QObject>
#include <memory>

class FFRecorder;

namespace subrec::shell {

// GUI thread only. Read-only subscription to the existing global FFRecorder;
// the recorder must outlive this object. No recording control is exposed.
class ShellStateSource : public QObject {
    Q_OBJECT

public:
    explicit ShellStateSource(FFRecorder &recorder, QObject *parent = nullptr);
    ~ShellStateSource() override;
    // WindowController supplies actual visibility; this source leaves Shown.
    ShellSnapshot snapshot() const;

signals:
    // Forward changes in captureState, captureTimeText, captureError,
    // captureDevices, outputPath and outputFileName so the snapshot stays fresh.
    void changed();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace subrec::shell

#endif // SUBREC_SHELL_STATE_SOURCE_H

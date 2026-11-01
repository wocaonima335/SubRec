#ifndef SUBREC_SHELL_TYPES_H
#define SUBREC_SHELL_TYPES_H

#include <QMetaType>
#include <QString>

namespace subrec::shell {

// Shell objects are created, used and destroyed on the GUI thread. These value
// types contain no recorder or platform dependencies.
// NoDevice means captureState == "Idle" with no available video device.
enum class RecState { Idle, NoDevice, Preparing, Running, Paused, Stopping, Failed };
enum class CloseAction { Ask, Tray, Exit };              // ask | tray | exit
enum class ExcludeMode { RecordingOnly, Always, Never }; // recording | always | never
enum class WindowVisibility { Shown, Minimized, HiddenToTray };
enum class TrayIconKind { Idle, Transition, Recording, Paused, Error };
enum class NotifyId {
    TrayHint,       // N1
    Saved,          // N2
    SubtitleResult, // N3: reserved until subtitle integration
    CaptureFailed  // N4
};
enum class CloseOutcome { AskUser, HideToTray, Exit };

struct ShellSnapshot {
    RecState state = RecState::Idle;
    QString timeText{};     // FFRecorder::captureTimeText
    QString captureError{};
    QString outputFile{};   // outputPath joined with outputFileName
    WindowVisibility window = WindowVisibility::Shown;
    bool subtitleFinishing = false; // Always false in batch 1.
    int subtitleProgress = 0;       // 0..100; zero in batch 1.
};

// Batch 1 omits the floating-bar menu item. Default construction is safe even
// before the first snapshot arrives; recording actions start disabled.
struct TrayMenuModel {
    QString toggleWindowText{};
    QString statusText{};
    TrayIconKind statusDot = TrayIconKind::Idle;
    QString startStopText{};
    bool startStopEnabled = false;
    QString pauseText{};
    bool pauseEnabled = false;
};

} // namespace subrec::shell

// Used by TrayController::notificationClicked; no namespace moc is required.
// Register with qRegisterMetaType before any old-style queued connection.
Q_DECLARE_METATYPE(subrec::shell::NotifyId)

#endif // SUBREC_SHELL_TYPES_H

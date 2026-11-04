#include "shell/shell_logic.h"
#include "test_support.h"
#include <array>
using namespace subrec::shell;
constexpr std::array<RecState, 7> states = {RecState::Idle, RecState::NoDevice, RecState::Preparing,
    RecState::Running, RecState::Paused, RecState::Stopping, RecState::Failed};

int main(int argc, char **argv)
{
    try {
        CHECK(argc == 2);
        const QString mode = QString::fromLocal8Bit(argv[1]);
        if (mode == "close") {
            // Every column of section 3.4, including persisted settings with C1 off.
            for (const bool tray : {false, true}) {
                CHECK(decideClose(tray, CloseAction::Ask) == (tray ? CloseOutcome::AskUser : CloseOutcome::Exit));
                CHECK(decideClose(tray, CloseAction::Tray) == (tray ? CloseOutcome::HideToTray : CloseOutcome::Exit));
                CHECK(decideClose(tray, CloseAction::Exit) == CloseOutcome::Exit);
            }
        } else if (mode == "minimize") {
            for (const bool tray : {false, true}) for (const bool hide : {false, true})
                CHECK(hideOnMinimize(tray, hide) == (tray && hide));
        } else if (mode == "exit") {
            const bool capturing[] = {false, false, true, true, true, true, false};
            for (size_t i = 0; i < states.size(); ++i) {
                CHECK(isCapturing(states[i]) == capturing[i]);
                for (const bool subtitle : {false, true}) for (const bool confirm : {false, true}) {
                    // The tray Exit row always enters this flow, independent of C3.
                    CHECK(exitNeedsConfirm(states[i], subtitle, confirm) == (confirm && (capturing[i] || subtitle)));
                    const int variant = states[i] == RecState::Stopping ? 3
                        : capturing[i] ? (subtitle ? 1 : 0) : (subtitle ? 2 : 0);
                    CHECK(exitConfirmVariant(states[i], subtitle) == variant);
                }
            }
        } else if (mode == "notifications") {
            // Section 3.5's shared permission gate covers N1-N6 (N3/N5/N6 reserved).
            // N5/N6 are now wired; both remain blocked for every capturing state.
            for (const auto state : {RecState::Preparing, RecState::Running, RecState::Paused, RecState::Stopping}) {
                CHECK(!notificationAllowed(WindowVisibility::HiddenToTray, state));
                CHECK(!notificationAllowed(WindowVisibility::Minimized, state));
            }
            for (int notification = 1; notification <= 6; ++notification)
                for (auto visibility : {WindowVisibility::Shown, WindowVisibility::Minimized, WindowVisibility::HiddenToTray})
                    for (size_t i = 0; i < states.size(); ++i) {
                        const bool idle = i == 0 || i == 1 || i == 6;
                        CHECK(notificationAllowed(visibility, states[i]) == (visibility != WindowVisibility::Shown && idle));
                    }
        } else if (mode == "exclusion") {
            for (auto mode : {ExcludeMode::RecordingOnly, ExcludeMode::Always, ExcludeMode::Never})
                for (bool supported : {false, true}) for (bool active : {false, true})
                    CHECK(shouldExclude(mode, active, supported) == (supported && mode != ExcludeMode::Never
                        && (active || mode == ExcludeMode::Always)));
        } else if (mode == "states") {
            const char *names[] = {"Idle", "Idle", "Preparing", "Running", "Paused", "Stopping", "Failed"};
            const TrayIconKind icons[] = {TrayIconKind::Idle, TrayIconKind::Idle, TrayIconKind::Transition,
                TrayIconKind::Recording, TrayIconKind::Paused, TrayIconKind::Transition, TrayIconKind::Error};
            for (size_t i = 0; i < states.size(); ++i) {
                CHECK(recStateFrom(QString::fromLatin1(names[i]), i != 1) == states[i]);
                CHECK(trayIconFor(states[i]) == icons[i]);
                if (i > 1) CHECK(recStateFrom(QString::fromLatin1(names[i]), false) == states[i]);
            }
            CHECK(recStateFrom("Unknown", true) == RecState::Failed);
        } else CHECK(false);
        return 0;
    } catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

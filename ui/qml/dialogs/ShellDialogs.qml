import QtQuick

// Module U4 (ZCode). Host for the shell dialogs D1 and D7, driven only by ShellView signals
// (closeChoiceRequested / exitConfirmRequested). Placed once in AppWindow.
// FROZEN API: none (self-contained).
Item {
    id: root

    CloseWindowDialog {
        id: closeWindowDialog
        onChosen: (toTray, remember) => ShellView.resolveCloseChoice(toTray, remember)
        onCancelled: ShellView.cancelExit()
    }

    QuitConfirmDialog {
        id: quitConfirmDialog
        onConfirmed: ShellView.confirmExit()
        onCancelled: ShellView.cancelExit()
    }

    Connections {
        target: ShellView
        function onCloseChoiceRequested(capturing: bool, timeText: string) {
            closeWindowDialog.openFor(capturing, timeText)
        }
        function onExitConfirmRequested(variant: int, timeText: string) {
            quitConfirmDialog.openFor(variant, timeText)
        }
    }
}

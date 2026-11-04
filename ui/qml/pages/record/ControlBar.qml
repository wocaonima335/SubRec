// Module U5 (ZCode). P1 area C (Card): StatusPill (RecorderView.uiState), SubtitleBadge
// (x fixed at +116 from the card's left inner edge), timer (RecorderView.elapsedText,
// colour per §3.2.3), RecordPauseButton + RecordMainButton with captions, save area
// (OutputNaming / RecorderView.outputPath, file line = OutputNaming.previewFileName when idle,
// RecorderView.currentOutputFile while capturing), open-folder IconButton.
// Button actions: main -> ShellView.requestStart() (idle/failed) or requestStop()
// (running/paused); pause -> requestPause()/requestResume(). Tooltips append
// HotkeyController.shortcutLabel(...) when non-empty.
// FROZEN API: none.
Card {
    id: root
    padding: Theme.space16

    // TODO(U5): full layout per §3.2.2 C.
    RecordMainButton {
        anchors.centerIn: parent
        uiState: RecorderView.uiState
        onClicked: {
            if (RecorderView.uiState === "running" || RecorderView.uiState === "paused")
                ShellView.requestStop()
            else
                ShellView.requestStart()
        }
    }
}

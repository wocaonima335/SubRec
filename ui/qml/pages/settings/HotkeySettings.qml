import QtQuick

// Module H3 (Codex). P3-5 快捷键 (UI_REDESIGN §3.5 P3-5 = MINIMIZE_DESIGN §6.4.3), batch 2.
//   intro; 录制: KeySequenceField(action ToggleRecord) + clear IconButton, KeySequenceField
//   (TogglePause) + clear; 窗口: KeySequenceField(ToggleWindow) + clear; 窗口内快捷键:
//   read-only list with KeyCaps (F9, F10, Ctrl 1/2/3); "恢复默认" ->
//   HotkeyController.restoreDefaults(). A row grows 64 -> 84 while it shows a message.
// FROZEN API: none.
Column {
    id: root
    spacing: 0

    // TODO(H3): rows.
    FormGroupTitle { text: "录制" }
}

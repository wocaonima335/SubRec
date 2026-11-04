import QtQuick

// Module U7 (Codex). P3-1 常规 (UI_REDESIGN §3.5 P3-1 = MINIMIZE_DESIGN §6.4.2), 1068-px tall.
// Groups and bindings:
//   外观: 主题 SegmentedControl [跟随系统|深色|浅色] <-> ThemeController.mode
//   窗口与托盘: C1 ShellView.trayEnabled, C2 minimizeToTray, C3 closeAction (AppComboBox
//     ask/tray/exit), C4 startMinimized, C7 hideOnRecordStart, C5 floatingBar
//     (whenHidden/always/never). C1 off or !ShellView.trayAvailable -> C2/C3/C4 disabled
//     with tooltip; C3 shows "退出字录".
//   录像画面: C6 excludeFromCapture (recording/always/never); !excludeSupported -> disabled
//     "不支持" + warning description; DirectShow line per §6.4.2 table.
//   录制结束后: AppPreferences.afterRecordAction (toast/openFolder/library); 退出确认
//     ShellView.confirmExitWhileRecording.
// Nothing on this page is locked while recording.
// FROZEN API: none.
Column {
    id: root
    spacing: 0

    // TODO(U7): rows.
    FormGroupTitle { text: "外观" }
}

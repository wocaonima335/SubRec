import QtQuick

// Module U7 (Codex). P3-6 高级 (UI_REDESIGN §3.5 P3-6).
//   屏幕采集方式 🔒: AppComboBox two-line options auto/ddagrab/dshow <->
//     RecorderView.screenCaptureBackend / setScreenCaptureBackend; disabled with the warning
//     line when screenCaptureBackendFromEnvironment.
//   日志文件: AppInfo.logFilePath (mono) + "打开所在文件夹" (DesktopActions.revealInFolder).
// FROZEN API: none.
Column {
    id: root
    spacing: 0

    // TODO(U7): rows.
    FormGroupTitle { text: "屏幕采集" }
}

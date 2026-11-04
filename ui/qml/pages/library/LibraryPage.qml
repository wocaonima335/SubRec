import QtQuick

// Module U6 (Codex). P2 录像库 (UI_REDESIGN §3.3): title, search AppTextField
// (-> RecordingLibrary.all.filterText), refresh IconButton (RecordingLibrary.refresh),
// "打开保存文件夹" (DesktopActions.openFolder(RecordingLibrary.folder)), summary line
// (RecordingLibrary.summaryText), list Card with RecordingTableHeader + ListView of
// RecordingRow over RecordingLibrary.all.
// States: L1 rows; L2 empty (EmptyState: "去录制" -> Navigation.open("record"),
// "更改保存位置" -> Navigation.open("settings/output")); L3 folderStatus != "ok" (danger
// InlineAlert); L4 filter matched nothing; L5 the active recording row (isRecording).
// Keys: Enter = play, Delete = DialogRequests.deleteRecording(...).
// FROZEN API: none.
Item {
    id: root

    // TODO(U6): full page.
    Text {
        x: Theme.pagePadding
        y: Theme.pagePadding
        text: "录像库"
        font: Theme.h1Font
        color: Theme.textPrimary
    }
}

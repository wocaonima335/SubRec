import QtQuick
import QtQuick.Dialogs

// Module U7 (Codex). P3-2 录制与输出 (UI_REDESIGN §3.5 P3-2).
//   保存文件夹 🔒: wide row, mono AppTextField (commit on editingFinished ->
//     OutputNaming.outputDir; errorText = OutputNaming.outputDirError) + "浏览…" -> FolderDialog
//     (D6: title "选择保存文件夹", currentFolder = current outputDir).
//   文件名 🔒: AppTextField (suffixText ".mp4") -> OutputNaming.fileNameTemplate;
//     errorText = OutputNaming.templateError; preview line OutputNaming.previewFileName.
//   输出格式 (read-only "MP4（H.264 视频 + AAC 音频）"), 视频编码器 (read-only
//     RecorderView.selectedVideoEncoder + (硬件/软件), info icon tooltip = encoder detail,
//     "开始录制后确定" when empty).
// FROZEN: the FolderDialog keeps objectName "folderDialog" (AutoTest ui_probe looks it up).
Column {
    id: root
    spacing: 0

    // TODO(U7): rows.
    FormGroupTitle { text: "保存位置" }

    FolderDialog {
        id: folderDialog
        objectName: "folderDialog"
        title: "选择保存文件夹"
    }
}

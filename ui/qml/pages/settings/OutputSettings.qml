import QtQuick
import QtQuick.Layouts
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
ColumnLayout {
    id: root
    spacing: 0

    // TODO(U7): rows.
    FormGroupTitle {
        text: "保存位置"
    }
    FormRow {
        wide: true
        title: "保存文件夹"
        description: "录像和字幕文件会保存到此文件夹"
        control: RowLayout {
            Layout.fillWidth: true
            AppTextField {
                Layout.fillWidth: true
                mono: true
                locked: RecorderView.capturing
                text: OutputNaming.outputDir
                errorText: OutputNaming.outputDirError
                onEditingFinished: OutputNaming.outputDir = text
            }
            AppButton {
                text: "浏览…"
                enabled: !RecorderView.capturing
                onClicked: folderDialog.open()
            }
        }
    }
    FormRow {
        wide: true
        title: "文件名模板"
        description: "支持 {日期}、{时间}、{来源}"
        control: AppTextField {
            width: 464
            locked: RecorderView.capturing
            suffixText: ".mp4"
            text: OutputNaming.fileNameTemplate
            errorText: OutputNaming.templateError
            onEditingFinished: OutputNaming.fileNameTemplate = text
        }
    }
    Text {
        font: Theme.captionFont
        text: "示例：" + OutputNaming.previewFileName
        color: Theme.textSecondary
    }
    FormGroupTitle {
        text: "输出格式与编码器"
    }
    FormRow {
        title: "输出格式"
        control: Text {
            font: Theme.captionFont
            text: "MP4（H.264 视频 + AAC 音频）"
            color: Theme.textSecondary
        }
    }
    FormRow {
        title: "视频编码器"
        description: RecorderView.selectedVideoEncoderDetail
        control: Text {
            font: Theme.captionFont
            text: RecorderView.selectedVideoEncoder.length > 0 ? RecorderView.selectedVideoEncoder + (RecorderView.selectedVideoEncoderIsHardware ? "（硬件）" : "（软件）") : "开始录制后确定"
            color: Theme.textSecondary
        }
    }

    FolderDialog {
        id: folderDialog
        objectName: "folderDialog"
        title: "选择保存文件夹"
        currentFolder: "file://" + OutputNaming.outputDir
        onAccepted: OutputNaming.setOutputFolderUrl(selectedFolder)
    }
}

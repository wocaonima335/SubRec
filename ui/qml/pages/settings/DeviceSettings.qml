import QtQuick

// Module U7 (Codex). P3-3 音频与设备 (UI_REDESIGN §3.5 P3-3).
//   视频设备 🔒: AppComboBox of screens + cameras, secondaryText "屏幕"/"摄像头"
//     -> RecorderView.selectVideoCaptureDevice
//   系统声音设备 🔒 / 麦克风设备 🔒: RecorderView.devicesOfKind("system-audio"/"microphone")
//     -> selectAudioCaptureDevice / selectMicrophoneCaptureDevice
//   能否同时录两路: read-only from RecorderView.audioMixingAvailable
//   设备列表: "共 {n} 个设备" + "刷新设备" (RecorderView.refreshCaptureDevices); empty list ->
//     warning InlineAlert.
// FROZEN API: none.
Column {
    id: root
    spacing: 0

    // TODO(U7): rows.
    FormGroupTitle { text: "视频来源" }
}

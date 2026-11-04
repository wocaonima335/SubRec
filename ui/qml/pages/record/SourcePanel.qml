// Module U5 (ZCode). P1 area D (Card, inner width 248): three sections.
// - 视频来源: SegmentedControl [屏幕][摄像头] (currentValue = RecorderView.selectedVideoKind,
//   activated -> RecorderView.openSource(kind)); AppComboBox of RecorderView.devicesOfKind(kind)
//   -> selectVideoCaptureDevice.
// - 音频: two rows (系统声音 / 麦克风): AppSwitch (audioMode contains the source) ->
//   RecorderView.setAudioMode(...) following the old OptionsArea rules (A1–A6, mixing needs
//   audioMixingAvailable); LevelMeter (AudioLevelMonitor; see plan §6 for the one-meter
//   decision); compact AppComboBox of the kind's devices; explanation line A2/A3.
// - AI 字幕: AppSwitch bound to SubtitleView.enabled (opening with no model -> SubtitleView
//   emits modelDownloadPromptRequested, D2 opens by itself); description + action per
//   SubtitleView.uiState (U1–U10, §3.2.4); "字幕设置 ›" -> Navigation.open("settings/subtitle");
//   "下载模型" -> ModelCatalog.requestDownload(SubtitleView.modelId); "取消下载" ->
//   ModelCatalog.cancelDownload; "在文件夹中显示" -> SubtitleView.openLastSrtFolder().
// Everything is locked (not disabled) while RecorderView.capturing (§3.2.5).
// FROZEN API: none.
Card {
    id: root
    padding: Theme.space16

    // TODO(U5): sections per §3.2.2 D, §3.2.4, §3.2.5.
}

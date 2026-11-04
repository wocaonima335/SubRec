pragma Singleton
import QtQuick

// Module U6/U8 (Codex). Request bus for content dialogs; ContentDialogs listens.
// FROZEN API:
//   deleteRecording(filePath, srtPath)   -> D5 (srtPath "" hides the subtitle checkbox)
//   deleteModel(modelId)                 -> D4
//   showLicense(modelId, readOnly)       -> D3 (readOnly: P3-4 "模型许可" link)
//   promptModelDownload(modelId)         -> D2
// D2/D3 are also opened from C++ signals (SubtitleView.modelDownloadPromptRequested,
// ModelCatalog.licenseRequired) inside ContentDialogs.
QtObject {
    signal deleteRecordingRequested(string filePath, string srtPath)
    signal deleteModelRequested(string modelId)
    signal licenseRequested(string modelId, bool readOnly)
    signal modelDownloadPromptRequested(string modelId)

    function deleteRecording(filePath: string, srtPath: string) { deleteRecordingRequested(filePath, srtPath) }
    function deleteModel(modelId: string) { deleteModelRequested(modelId) }
    function showLicense(modelId: string, readOnly: bool) { licenseRequested(modelId, readOnly) }
    function promptModelDownload(modelId: string) { modelDownloadPromptRequested(modelId) }
}

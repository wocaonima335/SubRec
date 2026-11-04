import QtQuick

// Module U8 (Codex; D5 part by U6). Host for D2–D5. Placed once in AppWindow.
// FROZEN API: none (self-contained; driven by DialogRequests and C++ signals).
Item {
    id: root

    ModelDownloadPromptDialog { id: d2 }
    ModelLicenseDialog { id: d3 }
    DeleteModelDialog { id: d4 }
    DeleteRecordingDialog { id: d5 }

    Connections {
        target: DialogRequests
        function onDeleteRecordingRequested(filePath: string, srtPath: string) { d5.openFor(filePath, srtPath) }
        function onDeleteModelRequested(modelId: string) { d4.openFor(modelId) }
        function onLicenseRequested(modelId: string, readOnly: bool) { d3.openFor(modelId, readOnly) }
        function onModelDownloadPromptRequested(modelId: string) { d2.openFor(modelId) }
    }
    Connections {
        target: SubtitleView
        function onModelDownloadPromptRequested(modelId: string) { d2.openFor(modelId) }
    }
    Connections {
        target: ModelCatalog
        function onLicenseRequired(modelId: string) { d3.openFor(modelId, false) }
    }
}

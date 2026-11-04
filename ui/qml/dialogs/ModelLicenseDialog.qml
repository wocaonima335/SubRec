import QtQuick

// Module U8 (Codex). D3 licence, 560×510, accept mode and read-only mode (§3.6 D3).
// FROZEN API: openFor(modelId, readOnly). Accept -> ModelCatalog.acceptLicenseAndDownload.
// No full text in the manifest -> "在浏览器中打开" must be clicked before the checkbox enables.
AppDialog {
    id: root
    property string modelId: ""
    property bool readOnly: false

    dialogWidth: 560
    title: readOnly ? "模型许可" : "下载前请阅读模型许可"
    primaryText: readOnly ? "关闭" : "同意并下载"
    secondaryText: readOnly ? "" : "取消"

    function openFor(id: string, ro: bool) {
        root.modelId = id
        root.readOnly = ro
        open()
    }

    // TODO(U8): licence text area, link, agree checkbox gating primaryEnabled.
    onAccepted: { close(); if (!readOnly) ModelCatalog.acceptLicenseAndDownload(modelId) }
    onRejected: close()
}

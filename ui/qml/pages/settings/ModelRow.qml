import QtQuick

// Module U8 (Codex). P3-4 model row, 768×88 (§3.5 P3-4 "模型行组件", states M1–M6).
// FROZEN API (ModelListModel roles): modelId, displayName, tag, summary, licenseName,
// status, progress, bytesDone, bytesTotal, errorText, selected, inUse.
// Actions: select radio -> SubtitleView.modelId = modelId (only when installed);
// 下载/重试/重新下载 -> ModelCatalog.requestDownload; 取消 -> cancelDownload;
// trash -> DialogRequests.deleteModel; licence link -> DialogRequests.showLicense(id, true).
Rectangle {
    id: root
    property string modelId: ""
    property string displayName: ""
    property string tag: ""
    property string summary: ""
    property string licenseName: ""
    property string status: "notInstalled"
    property real progress: 0
    property real bytesDone: 0
    property real bytesTotal: 0
    property string errorText: ""
    property bool selected: false
    property bool inUse: false

    implicitWidth: 768
    implicitHeight: 88
    radius: Theme.radiusMd
    color: Theme.bgSurface2
    border.width: selected ? 1.5 : 1
    border.color: selected ? Theme.accentDefault : Theme.borderDefault

    // TODO(U8): left info block, right status area per M1–M6.
}

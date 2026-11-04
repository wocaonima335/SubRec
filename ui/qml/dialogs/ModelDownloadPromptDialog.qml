import QtQuick

// Module U8 (Codex). D2 "启用 AI 字幕需要下载模型", 440×244 (UI_REDESIGN §3.6 D2).
// FROZEN API: openFor(modelId). "稍后" keeps the switch on (U2); "下载…" closes and calls
// ModelCatalog.requestDownload(modelId) (which opens D3 when the licence is not accepted).
AppDialog {
    id: root
    property string modelId: ""

    title: "启用 AI 字幕需要下载模型"
    primaryText: "下载…"
    secondaryText: "稍后"

    function openFor(id: string) {
        root.modelId = id;
        open();
    }

    // TODO(U8): body/note texts with ModelCatalog.modelInfo(modelId).sizeText.
    Column {
        width: parent.width
        spacing: Theme.space8
        Text {
            text: "需要下载模型才能启用 AI 字幕。"
            color: Theme.textPrimary
        }
        Text {
            text: String(ModelCatalog.modelInfo(root.modelId).sizeText || "下载大小以模型清单为准")
            color: Theme.textSecondary
        }
    }

    onAccepted: {
        close();
        ModelCatalog.requestDownload(modelId);
    }
    onRejected: close()
}

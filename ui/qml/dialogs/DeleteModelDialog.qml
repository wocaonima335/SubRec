import QtQuick

// Module U8 (Codex). D4 delete model, 440×184 (208 with the "字幕将不可用" warning).
// FROZEN API: openFor(modelId). Danger primary, Enter does not confirm.
// Failure (ModelBusy etc.) -> Toasts.show("danger", "无法删除模型", error, [], 0).
AppDialog {
    id: root
    property string modelId: ""

    title: "删除模型？"
    primaryText: "删除"
    primaryKind: "danger"

    function openFor(id: string) {
        root.modelId = id
        open()
    }

    // TODO(U8): title with display name, size text, conditional warning line.
    onAccepted: {
        close()
        const error = ModelCatalog.remove(modelId)
        if (error.length > 0)
            Toasts.show("danger", "无法删除模型", error, [], 0)
    }
    onRejected: close()
}

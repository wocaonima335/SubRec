import QtQuick
import QtQuick.Layouts

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
    implicitHeight: root.errorText.length > 0 ? 112 : 88
    radius: Theme.radiusMd
    color: Theme.bgSurface2
    border.width: selected ? 1.5 : 1
    border.color: selected ? Theme.accentDefault : Theme.borderDefault

    // TODO(U8): left info block, right status area per M1–M6.
    AppProgressBar {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.space8
        visible: root.status === "downloading" || root.status === "verifying"
        value: root.progress
        indeterminate: root.status === "verifying"
    }
    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.space12
        spacing: Theme.space12
        AppRadioButton {
            checked: root.selected
            enabled: root.status === "installed" && !SubtitleView.settingsLocked
            onClicked: SubtitleView.modelId = root.modelId
        }
        Text {
            text: root.displayName
            font: Theme.bodyStrongFont
            color: Theme.textPrimary
        }
        Text {
            text: root.tag === "recommended" ? "推荐" : "备选"
            color: root.tag === "recommended" ? Theme.accentText : Theme.textSecondary
            font: Theme.captionFont
        }
        Text {
            font: Theme.captionFont
            Layout.fillWidth: true
            text: root.errorText.length > 0 ? root.errorText : root.summary
            color: Theme.textSecondary
            elide: Text.ElideRight
        }
        Text {
            font: Theme.captionFont
            visible: root.status === "downloading"
            text: Math.round(root.progress * 100) + "% · " + Math.round(root.bytesDone / 1048576) + " / " + Math.round(root.bytesTotal / 1048576) + " MB"
            color: Theme.textSecondary
        }
        AppButton {
            small: true
            text: root.status === "installed" ? "已安装" : root.status === "downloading" ? "取消" : root.status === "verifying" ? "校验中…" : root.status === "stale" ? "重新下载" : root.status === "failed" ? "重试" : "下载"
            enabled: root.status !== "installed" && root.status !== "verifying"
            onClicked: {
                if (root.status === "downloading")
                    ModelCatalog.cancelDownload(root.modelId);
                else
                    ModelCatalog.requestDownload(root.modelId);
            }
        }
        AppButton {
            small: true
            text: "许可"
            disabledReason: "模型许可"
            onClicked: DialogRequests.showLicense(root.modelId, true)
        }
        AppButton {
            small: true
            text: "删除"
            enabled: root.status === "installed" && !root.inUse
            disabledReason: "正在使用，不能删除"
            onClicked: DialogRequests.deleteModel(root.modelId)
        }
    }
}

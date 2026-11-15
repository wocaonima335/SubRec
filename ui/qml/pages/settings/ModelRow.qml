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
    implicitHeight: compact ? Theme.space12 + info.implicitHeight + Theme.space8 + actions.implicitHeight + Theme.space16
                            : root.errorText.length > 0 ? 112 : 88
    radius: Theme.radiusMd
    color: Theme.bgSurface2
    border.width: selected ? 1.5 : 1
    border.color: selected ? Theme.accentDefault : Theme.borderDefault

    // UI 1114 (narrow settings form): when the info block (at least 280) and the buttons do
    // not fit side by side, the buttons move under the info and wrap; texts elide.
    // One-line width of the buttons (all three are always shown).
    readonly property real __actionsWidth: statusButton.implicitWidth + licenseButton.implicitWidth
                                           + deleteButton.implicitWidth + 2 * actions.columnSpacing
    readonly property bool compact: width > 0 && width < Theme.space12 * 3 + 280 + __actionsWidth

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
        id: info
        x: Theme.space12
        y: root.compact ? Theme.space12 : 0
        width: root.width - Theme.space12 * 2 - (root.compact ? 0 : actions.width + Theme.space12)
        height: root.compact ? implicitHeight : root.height
        spacing: Theme.space12
        AppRadioButton {
            checked: root.selected
            enabled: root.status === "installed" && !SubtitleView.settingsLocked
            onClicked: SubtitleView.modelId = root.modelId
        }
        Text {
            Layout.fillWidth: true
            Layout.maximumWidth: implicitWidth
            text: root.displayName
            font: Theme.bodyStrongFont
            color: Theme.textPrimary
            elide: Text.ElideRight
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
            Layout.fillWidth: true
            Layout.maximumWidth: implicitWidth
            text: Math.round(root.progress * 100) + "% · " + Math.round(root.bytesDone / 1048576) + " / " + Math.round(root.bytesTotal / 1048576) + " MB"
            color: Theme.textSecondary
            elide: Text.ElideRight
        }
    }
    // Buttons at their own widths (no width set): 3 per line when they fit, else 2 or 1.
    GridLayout {
        id: actions
        readonly property real available: root.compact ? root.width - Theme.space12 * 2 : root.__actionsWidth
        x: root.compact ? Theme.space12 : root.width - Theme.space12 - width
        y: root.compact ? info.y + info.height + Theme.space8 : (root.height - height) / 2
        columnSpacing: Theme.space12
        rowSpacing: Theme.space8
        columns: available >= root.__actionsWidth ? 3
               : available >= statusButton.implicitWidth + licenseButton.implicitWidth + columnSpacing ? 2 : 1
        AppButton {
            id: statusButton
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
            id: licenseButton
            small: true
            text: "许可"
            disabledReason: "模型许可"
            onClicked: DialogRequests.showLicense(root.modelId, true)
        }
        AppButton {
            id: deleteButton
            small: true
            text: "删除"
            enabled: root.status === "installed" && !root.inUse
            disabledReason: "正在使用，不能删除"
            onClicked: DialogRequests.deleteModel(root.modelId)
        }
    }
}

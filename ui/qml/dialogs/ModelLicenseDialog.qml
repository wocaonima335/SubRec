import QtQuick
import QtQuick.Controls.Basic

// Module U8 (Codex). D3 licence, 560×510, accept mode and read-only mode (§3.6 D3).
// FROZEN API: openFor(modelId, readOnly). Accept -> ModelCatalog.acceptLicenseAndDownload.
// No full text in the manifest -> "在浏览器中打开" must be clicked before the checkbox enables.
AppDialog {
    id: root
    property string modelId: ""
    property bool readOnly: false
    property bool openedLicense: false
    readonly property var info: ModelCatalog.modelInfo(modelId)
    primaryEnabled: readOnly || (agreement.checked && (String(info.licenseText || "").length > 0 || openedLicense))

    dialogWidth: 560
    title: readOnly ? "模型许可" : "下载前请阅读模型许可"
    primaryText: readOnly ? "关闭" : "同意并下载"
    secondaryText: readOnly ? "" : "取消"

    function openFor(id: string, ro: bool) {
        root.openedLicense = false;
        agreement.checked = false;
        root.modelId = id;
        root.readOnly = ro;
        open();
    }

    // TODO(U8): licence text area, link, agree checkbox gating primaryEnabled.
    Column {
        width: parent.width
        spacing: Theme.space8
        Text {
            font: Theme.bodyFont
            text: String(root.info.licenseName || "")
            color: Theme.textPrimary
        }
        ScrollView {
            width: parent.width
            height: 240
            contentWidth: availableWidth
            Text {
                font: Theme.captionFont
                width: parent.width
                text: String(root.info.licenseText || "").length > 0 ? String(root.info.licenseText) : "请在浏览器中阅读许可原文。"
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }
        }
        AppButton {
            enabled: String(root.info.licenseUrl || "").length > 0
            text: "在浏览器中打开"
            onClicked: root.openedLicense = DesktopActions.openUrl(String(root.info.licenseUrl || ""))
        }
        AppCheckBox {
            id: agreement
            enabled: String(root.info.licenseText || "").length > 0 || root.openedLicense
            visible: !root.readOnly
            text: "我已阅读并同意许可"
        }
    }
    onAccepted: {
        if (!primaryEnabled)
            return;
        close();
        if (!readOnly)
            ModelCatalog.acceptLicenseAndDownload(modelId);
    }
    onRejected: close()
}

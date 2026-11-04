import QtQuick

// Module U7 (Codex). P3-7 关于 (UI_REDESIGN §3.5 P3-7): BrandLogo size 48, "字录 SubRec",
// AppInfo.versionText; 许可 group: 软件许可 (DesktopActions.openFile(AppInfo.licenseFilePath)),
// 第三方组件 (thirdPartyLicensesPath), 识别模型许可 "前往" -> Navigation.open("settings/subtitle").
// FROZEN API: none.
Column {
    id: root
    spacing: 0

    // TODO(U7): header and rows.
    BrandLogo { size: 48 }
}

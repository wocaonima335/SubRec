import QtQuick
import QtQuick.Layouts

// Module U7 (Codex). P3-7 关于 (UI_REDESIGN §3.5 P3-7): BrandLogo size 48, "字录 SubRec",
// AppInfo.versionText; 许可 group: 软件许可 (DesktopActions.openFile(AppInfo.licenseFilePath)),
// 第三方组件 (thirdPartyLicensesPath), 识别模型许可 "前往" -> Navigation.open("settings/subtitle").
// FROZEN API: none.
ColumnLayout {
    id: root
    spacing: Theme.space16

    // TODO(U7): header and rows.
    BrandLogo {
        size: 48
    }
    Text {
        text: "字录 SubRec"
        font: Theme.h1Font
        color: Theme.textPrimary
    }
    Text {
        font: Theme.captionFont
        text: AppInfo.versionText
        color: Theme.textSecondary
    }
    FormGroupTitle {
        text: "许可"
    }
    RowLayout {
        AppButton {
            text: "软件许可"
            onClicked: DesktopActions.openFile(AppInfo.licenseFilePath)
        }
        AppButton {
            text: "第三方组件"
            onClicked: DesktopActions.openFile(AppInfo.thirdPartyLicensesPath)
        }
        AppButton {
            visible: SubtitleView.serviceAttached
            text: "识别模型许可"
            onClicked: Navigation.open("settings/subtitle")
        }
    }
}

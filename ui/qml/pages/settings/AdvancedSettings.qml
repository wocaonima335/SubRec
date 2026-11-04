import QtQuick
import QtQuick.Layouts

// Module U7 (Codex). P3-6 高级 (UI_REDESIGN §3.5 P3-6).
//   屏幕采集方式 🔒: AppComboBox two-line options auto/ddagrab/dshow <->
//     RecorderView.screenCaptureBackend / setScreenCaptureBackend; disabled with the warning
//     line when screenCaptureBackendFromEnvironment.
//   日志文件: AppInfo.logFilePath (mono) + "打开所在文件夹" (DesktopActions.revealInFolder).
// FROZEN API: none.
ColumnLayout {
    id: root
    spacing: Theme.space16

    // TODO(U7): rows.
    FormGroupTitle {
        text: "屏幕采集"
    }
    FormRow {
        title: "屏幕采集方式"
        description: RecorderView.screenCaptureBackendFromEnvironment ? "由环境变量指定，无法在此修改" : "自动选择最合适的后端"
        control: AppComboBox {
            enabled: !RecorderView.screenCaptureBackendFromEnvironment && !RecorderView.capturing
            model: [
                {
                    text: "自动",
                    value: "auto"
                },
                {
                    text: "Desktop Duplication",
                    value: "ddagrab"
                },
                {
                    text: "DirectShow",
                    value: "dshow"
                },
            ]
            currentIndex: Math.max(0, ["auto", "ddagrab", "dshow"].indexOf(RecorderView.screenCaptureBackend))
            onActivated: function (index) {
                RecorderView.setScreenCaptureBackend(model[index].value);
            }
        }
    }
    FormGroupTitle {
        text: "日志"
    }
    FormRow {
        wide: true
        title: "日志文件"
        control: RowLayout {
            Text {
                Layout.fillWidth: true
                text: AppInfo.logFilePath
                elide: Text.ElideMiddle
                color: Theme.textSecondary
                font: Theme.monoSmallFont
            }
            AppButton {
                text: "打开所在文件夹"
                onClicked: DesktopActions.revealInFolder(AppInfo.logFilePath)
            }
        }
    }
}

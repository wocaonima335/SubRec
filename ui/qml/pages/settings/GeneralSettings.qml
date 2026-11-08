import QtQuick
import QtQuick.Layouts

// Module U7 (Codex). P3-1 常规 (UI_REDESIGN §3.5 P3-1 = MINIMIZE_DESIGN §6.4.2), 1068-px tall.
// Groups and bindings:
//   外观: 主题 SegmentedControl [跟随系统|深色|浅色] <-> ThemeController.mode
//   窗口与托盘: C1 ShellView.trayEnabled, C2 minimizeToTray, C3 closeAction (AppComboBox
//     ask/tray/exit), C4 startMinimized, C7 hideOnRecordStart, C5 floatingBar
//     (whenHidden/always/never). C1 off or !ShellView.trayAvailable -> C2/C3/C4 disabled
//     with tooltip; C3 shows "退出字录".
//   录像画面: C6 excludeFromCapture (recording/always/never); !excludeSupported -> disabled
//     "不支持" + warning description; DirectShow line per §6.4.2 table.
//   录制结束后: AppPreferences.afterRecordAction (toast/openFolder/library); 退出确认
//     ShellView.confirmExitWhileRecording.
// Nothing on this page is locked while recording.
// FROZEN API: none.
ColumnLayout {
    id: root
    spacing: Theme.space16

    // TODO(U7): rows.
    FormGroupTitle {
        text: "外观"
    }
    FormRow {
        title: "主题"
        control: SegmentedControl {
            model: [
                {
                    text: "跟随系统",
                    value: "system"
                },
                {
                    text: "深色",
                    value: "dark"
                },
                {
                    text: "浅色",
                    value: "light"
                },
            ]
            currentValue: ThemeController.mode
            onActivated: function (value) {
                ThemeController.mode = value;
            }
        }
    }

    FormGroupTitle {
        text: "窗口与托盘"
    }
    FormRow {
        title: "启用系统托盘"
        description: ShellView.trayAvailable ? "录制期间可在后台运行" : "当前系统不支持系统托盘"
        control: AppSwitch {
            backendChecked: ShellView.trayEnabled
            enabled: ShellView.trayAvailable
            onToggledTo: function (value) {
                ShellView.trayEnabled = value;
            }
        }
    }
    FormRow {
        title: "最小化到托盘"
        control: AppSwitch {
            backendChecked: ShellView.minimizeToTray
            enabled: ShellView.trayAvailable && ShellView.trayEnabled
            onToggledTo: function (value) {
                ShellView.minimizeToTray = value;
            }
        }
    }
    FormRow {
        title: "录制开始时隐藏窗口"
        control: AppSwitch {
            backendChecked: ShellView.hideOnRecordStart
            onToggledTo: function (value) {
                ShellView.hideOnRecordStart = value;
            }
        }
    }
    FormRow {
        title: "悬浮条"
        description: ShellView.excludeSupported ? "隐藏窗口后仍可控制录制" : "当前系统不支持窗口排除，悬浮条已禁用"
        control: AppComboBox {
            enabled: ShellView.excludeSupported
            model: [
                {
                    text: "窗口隐藏时显示",
                    value: "whenHidden"
                },
                {
                    text: "始终显示",
                    value: "always"
                },
                {
                    text: "从不显示",
                    value: "never"
                },
            ]
            currentIndex: Math.max(0, ["whenHidden", "always", "never"].indexOf(ShellView.floatingBar))
            onActivated: function (index) {
                ShellView.floatingBar = model[index].value;
            }
        }
    }
    FormRow {
        title: "关闭窗口时"
        control: AppComboBox {
            enabled: ShellView.trayAvailable && ShellView.trayEnabled
            model: [
                {
                    text: "每次询问",
                    value: "ask"
                },
                {
                    text: "最小化到托盘",
                    value: "tray"
                },
                {
                    text: "退出字录",
                    value: "exit"
                },
            ]
            currentIndex: !ShellView.trayAvailable || !ShellView.trayEnabled ? 2 : Math.max(0, ["ask", "tray", "exit"].indexOf(ShellView.closeAction))
            onActivated: function (index) {
                ShellView.closeAction = model[index].value;
            }
        }
    }

    FormGroupTitle {
        text: "录像画面"
    }
    FormRow {
        title: "录制时从画面中排除窗口"
        description: ShellView.excludeSupported ? "仅对桌面复制采集方式保证有效。开启后，截图工具、会议软件、远程协助也看不到字录窗口。" : "需要 Windows 10 2004 或更高版本；此系统上悬浮条会被录进画面。"
        control: AppComboBox {
            enabled: ShellView.excludeSupported
            model: [
                {
                    text: "录制时",
                    value: "recording"
                },
                {
                    text: "始终",
                    value: "always"
                },
                {
                    text: "从不",
                    value: "never"
                },
            ]
            currentIndex: Math.max(0, ["recording", "always", "never"].indexOf(ShellView.excludeFromCapture))
            onActivated: function (index) {
                ShellView.excludeFromCapture = model[index].value;
            }
        }
    }
    FormRow {
        title: "启动时最小化到托盘"
        control: AppSwitch {
            backendChecked: ShellView.startMinimized
            enabled: ShellView.trayAvailable && ShellView.trayEnabled
            onToggledTo: function (value) {
                ShellView.startMinimized = value;
            }
        }
    }
    FormGroupTitle {
        text: "录制结束后"
    }
    FormRow {
        title: "完成录制时"
        control: AppComboBox {
            model: [
                {
                    text: "显示提示",
                    value: "toast"
                },
                {
                    text: "打开文件夹",
                    value: "openFolder"
                },
                {
                    text: "打开录像库",
                    value: "library"
                }
            ]
            currentIndex: ["toast", "openFolder", "library"].indexOf(AppPreferences.afterRecordAction)
            onActivated: function (index) {
                AppPreferences.afterRecordAction = model[index].value;
            }
        }
    }
    FormRow {
        title: "录制中退出时确认"
        control: AppSwitch {
            backendChecked: ShellView.confirmExitWhileRecording
            onToggledTo: function (value) {
                ShellView.confirmExitWhileRecording = value;
            }
        }
    }
}

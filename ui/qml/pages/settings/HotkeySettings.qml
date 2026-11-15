import QtQuick
import QtQuick.Layouts

// Module H3 (Codex). P3-5 快捷键 (UI_REDESIGN §3.5 P3-5 = MINIMIZE_DESIGN §6.4.3), batch 2.
//   intro; 录制: KeySequenceField(action ToggleRecord) + clear IconButton, KeySequenceField
//   (TogglePause) + clear; 窗口: KeySequenceField(ToggleWindow) + clear; 窗口内快捷键:
//   read-only list with KeyCaps (F9, F10, Ctrl 1/2/3); "恢复默认" ->
//   HotkeyController.restoreDefaults(). A row grows 64 -> 84 while it shows a message (more
//   when the message wraps in a narrow field).
// FROZEN API: none.
ColumnLayout {
    id: root
    spacing: Theme.space16

    // TODO(H3): rows.
    Text {
        Layout.fillWidth: true
        font: Theme.captionFont
        text: "全局快捷键在应用最小化后仍然有效。"
        color: Theme.textSecondary
        wrapMode: Text.WordWrap
    }
    FormGroupTitle {
        text: "录制"
    }
    FormRow {
        title: "开始/停止录制"
        rowHeight: recordKey.message.length > 0 ? recordKey.implicitHeight + 20 : 64
        control: RowLayout {
            KeySequenceField {
                id: recordKey
                Layout.fillWidth: true
                Layout.maximumWidth: implicitWidth
                action: HotkeyController.ToggleRecord
            }
            AppButton {
                small: true
                text: "清除"
                onClicked: HotkeyController.clear(HotkeyController.ToggleRecord)
            }
        }
    }
    FormRow {
        title: "暂停/继续录制"
        rowHeight: pauseKey.message.length > 0 ? pauseKey.implicitHeight + 20 : 64
        control: RowLayout {
            KeySequenceField {
                id: pauseKey
                Layout.fillWidth: true
                Layout.maximumWidth: implicitWidth
                action: HotkeyController.TogglePause
            }
            AppButton {
                small: true
                text: "清除"
                onClicked: HotkeyController.clear(HotkeyController.TogglePause)
            }
        }
    }
    FormGroupTitle {
        text: "窗口"
    }
    FormRow {
        title: "显示/隐藏窗口"
        rowHeight: windowKey.message.length > 0 ? windowKey.implicitHeight + 20 : 64
        control: RowLayout {
            KeySequenceField {
                id: windowKey
                Layout.fillWidth: true
                Layout.maximumWidth: implicitWidth
                action: HotkeyController.ToggleWindow
            }
            AppButton {
                small: true
                text: "清除"
                onClicked: HotkeyController.clear(HotkeyController.ToggleWindow)
            }
        }
    }
    FormGroupTitle {
        text: "窗口内快捷键"
    }
    // The descriptions fill and elide so these rows never set a minimum width wider than the
    // form (the settings StackLayout would widen the whole page to it).
    RowLayout {
        Layout.fillWidth: true
        KeyCap {
            text: "F9"
        }
        Text {
            Layout.fillWidth: true
            font: Theme.captionFont
            text: "开始/停止录制"
            color: Theme.textSecondary
            elide: Text.ElideRight
        }
    }
    RowLayout {
        Layout.fillWidth: true
        KeyCap {
            text: "F10"
        }
        Text {
            Layout.fillWidth: true
            font: Theme.captionFont
            text: "暂停/继续录制"
            color: Theme.textSecondary
            elide: Text.ElideRight
        }
    }
    RowLayout {
        Layout.fillWidth: true
        KeyCap {
            text: "Ctrl"
        }
        KeyCap {
            text: "1/2/3"
        }
        Text {
            Layout.fillWidth: true
            font: Theme.captionFont
            text: "切换页面"
            color: Theme.textSecondary
            elide: Text.ElideRight
        }
    }
    AppButton {
        text: "恢复默认"
        onClicked: HotkeyController.restoreDefaults()
    }
}

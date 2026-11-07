import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Module U7 (Codex). P3 设置 frame (UI_REDESIGN §3.4): title, sub-navigation Card (7 dense
// NavItems, selection = Navigation.settingsPage, click -> Navigation.open("settings/<id>")),
// form Card (padding 24, vertical scroll). While RecorderView.capturing (or
// SubtitleView.uiState === "finishing" on the subtitle page) an info InlineAlert is inserted
// at the top and 🔒 controls go to the locked state. Settings apply immediately; no Save.
// Sub-page ids: general output devices subtitle hotkeys advanced about.
// FROZEN API: none.
Item {
    id: root

    // TODO(U7): title, sub-nav, form card with Flickable, recording alert.
    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.pagePadding
        spacing: Theme.space16

        Card {
            Layout.preferredWidth: 180
            Layout.fillHeight: true
            title: "设置"
            ColumnLayout {
                anchors.fill: parent
                Repeater {
                    model: [
                        {
                            text: "常规",
                            value: "general"
                        },
                        {
                            text: "录制与输出",
                            value: "output"
                        },
                        {
                            text: "音频与设备",
                            value: "devices"
                        },
                        {
                            text: "AI 字幕",
                            value: "subtitle"
                        },
                        {
                            text: "快捷键",
                            value: "hotkeys"
                        },
                        {
                            text: "高级",
                            value: "advanced"
                        },
                        {
                            text: "关于",
                            value: "about"
                        },
                    ]
                    delegate: AppButton {
                        id: navigationButton
                        required property var modelData
                        visible: modelData.value !== "subtitle" || SubtitleView.serviceAttached
                        Layout.fillWidth: true
                        text: navigationButton.modelData.text
                        variant: Navigation.settingsPage === navigationButton.modelData.value ? "primary" : "ghost"
                        onClicked: Navigation.open("settings/" + navigationButton.modelData.value)
                    }
                }
            }
        }

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                InlineAlert {
                    Layout.fillWidth: true
                    visible: RecorderView.capturing || (Navigation.settingsPage === "subtitle" && SubtitleView.settingsLocked)
                    kind: "info"
                    title: "录制进行中"
                    body: "部分设置会在录制结束后才能修改。"
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    StackLayout {
                        width: parent.width
                        implicitHeight: children[currentIndex] ? children[currentIndex].implicitHeight : 0
                        currentIndex: ["general", "output", "devices", "subtitle", "hotkeys", "advanced", "about"].indexOf(Navigation.settingsPage === "subtitle" && !SubtitleView.serviceAttached ? "general" : Navigation.settingsPage)

                        GeneralSettings {}
                        OutputSettings {}
                        DeviceSettings {}
                        SubtitleSettings {
                            visible: SubtitleView.serviceAttached
                        }
                        HotkeySettings {}
                        AdvancedSettings {}
                        AboutSettings {}
                    }
                }
            }
        }
    }
}

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
            // §3.4: 7 dense NavItems (36 px) with the main NavRail's Theme.space4 spacing,
            // written out like NavRail.qml. Not a Repeater: delegates created by a Repeater are
            // not QObject children of the Column, so QObject::findChild (tests, AutoTest probes)
            // cannot reach them by objectName.
            Column {
                objectName: "settingsSubNav"
                width: parent.width
                spacing: Theme.space4
                NavItem {
                    objectName: "settingsNav_general"
                    width: parent.width
                    dense: true
                    text: "常规"
                    selected: Navigation.settingsPage === "general"
                    onClicked: Navigation.open("settings/general")
                }
                NavItem {
                    objectName: "settingsNav_output"
                    width: parent.width
                    dense: true
                    text: "录制与输出"
                    selected: Navigation.settingsPage === "output"
                    onClicked: Navigation.open("settings/output")
                }
                NavItem {
                    objectName: "settingsNav_devices"
                    width: parent.width
                    dense: true
                    text: "音频与设备"
                    selected: Navigation.settingsPage === "devices"
                    onClicked: Navigation.open("settings/devices")
                }
                NavItem {
                    objectName: "settingsNav_subtitle"
                    visible: SubtitleView.serviceAttached
                    width: parent.width
                    dense: true
                    text: "AI 字幕"
                    selected: Navigation.settingsPage === "subtitle"
                    onClicked: Navigation.open("settings/subtitle")
                }
                NavItem {
                    objectName: "settingsNav_hotkeys"
                    width: parent.width
                    dense: true
                    text: "快捷键"
                    selected: Navigation.settingsPage === "hotkeys"
                    onClicked: Navigation.open("settings/hotkeys")
                }
                NavItem {
                    objectName: "settingsNav_advanced"
                    width: parent.width
                    dense: true
                    text: "高级"
                    selected: Navigation.settingsPage === "advanced"
                    onClicked: Navigation.open("settings/advanced")
                }
                NavItem {
                    objectName: "settingsNav_about"
                    width: parent.width
                    dense: true
                    text: "关于"
                    selected: Navigation.settingsPage === "about"
                    onClicked: Navigation.open("settings/about")
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

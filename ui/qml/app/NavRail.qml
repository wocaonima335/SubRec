import QtQuick

// Module U4 (ZCode). §3.1 navigation rail: 200 wide (64 when collapsed), items 录制 /
// 录像库 / 设置, version line at the bottom (AppInfo.versionText).
// FROZEN API: collapsed. Selection = Navigation.page; clicks call Navigation.open().
// Recording dot on "录制": RecorderView.uiState running -> record/default, paused -> warning/fg.
Rectangle {
    id: root
    property bool collapsed: false

    implicitWidth: collapsed ? Theme.navCollapsedWidth : Theme.navWidth
    color: Theme.bgApp

    // TODO(U4): three NavItems, dot colour, version text, right divider.
    Column {
        x: 12
        y: 16
        spacing: 4
        NavItem {
            text: "录制"
            iconName: "video"
            collapsed: root.collapsed
            selected: Navigation.page === "record"
            onClicked: Navigation.open("record")
        }
        NavItem {
            text: "录像库"
            iconName: "library"
            collapsed: root.collapsed
            selected: Navigation.page === "library"
            onClicked: Navigation.open("library")
        }
        NavItem {
            text: "设置"
            iconName: "settings"
            collapsed: root.collapsed
            selected: Navigation.page === "settings"
            onClicked: Navigation.open("settings")
        }
    }
}

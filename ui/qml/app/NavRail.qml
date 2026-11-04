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

    readonly property color __dotColor: RecorderView.uiState === "running" ? Theme.recordDefault
                                      : RecorderView.uiState === "paused" ? Theme.warningFg
                                      : "transparent"

    Column {
        x: Theme.space12
        y: Theme.space16
        spacing: Theme.space4
        NavItem {
            text: qsTr("录制")
            iconName: "video"
            collapsed: root.collapsed
            selected: Navigation.page === "record"
            dotColor: root.__dotColor
            onClicked: Navigation.open("record")
        }
        NavItem {
            text: qsTr("录像库")
            iconName: "library"
            collapsed: root.collapsed
            selected: Navigation.page === "library"
            onClicked: Navigation.open("library")
        }
        NavItem {
            text: qsTr("设置")
            iconName: "settings"
            collapsed: root.collapsed
            selected: Navigation.page === "settings"
            onClicked: Navigation.open("settings")
        }
    }

    // §3.1 version line at the bottom.
    Text {
        visible: !root.collapsed
        anchors.left: parent.left
        anchors.leftMargin: Theme.space24
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.space16
        text: AppInfo.versionText
        font: Theme.captionFont
        color: Theme.textTertiary
    }

    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 1
        color: Theme.borderDefault
    }
}

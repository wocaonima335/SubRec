import QtQuick

// Module U4 (ZCode). §3.1 title bar, 40 high, whole bar drags the window
// (Window.startSystemMove), double-click toggles maximize.
// FROZEN API:
//   window: the ApplicationWindow (for startSystemMove / visibility)
//   compact: hide the app name (narrow window)
//   signals minimizeClicked(), maximizeToggled(), closeClicked()
// Mini recording indicator (shown while RecorderView.capturing and Navigation.page !==
// "record") and the C2/C3-dependent tooltips (§6.6) are internal.
Rectangle {
    id: root
    property Window window
    property bool compact: false
    signal minimizeClicked()
    signal maximizeToggled()
    signal closeClicked()

    implicitHeight: Theme.titleBarHeight
    color: Theme.bgApp

    // TODO(U4): logo + name, drag/double-click, mini indicator, min/max/close IconButtons
    // (46×40, close hover Theme.closeButtonHover), bottom divider.
    BrandLogo {
        x: 16
        anchors.verticalCenter: parent.verticalCenter
    }
}

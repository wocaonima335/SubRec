import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Module U4 (ZCode). Root of the new interface: loadFromModule("SubRec.Ui", "AppWindow").
// Owns: frameless window, title bar, nav rail, page stack, global shortcuts, toast host,
// dialog hosts, the floating bar instance, window state persistence.
//
// FROZEN (integration contract with main.cpp, module INT-A):
// - The 8 root signals below and handleSettingItemClick(index, title) exist with the same
//   names as qml/Main.qml, so main.cpp's existing QObject::connect calls and AutoTest's
//   invokeMethod keep working unchanged. New-UI QML must NOT emit start/stop/pause signals
//   for its own buttons: it calls ShellView.request*() directly.
// - Exactly one GLView with objectName "previewView" (PreviewSurface.qml).
// - `visible` is set by main.cpp's shellStartHidden logic through the initialVisible
//   property (set before show), not from QML.
// - Child page files are instantiated by type name only; they talk back through the
//   Navigation / Toasts / DialogRequests singletons, never through ids in this file.
ApplicationWindow {
    id: appWindow
    property bool initialVisible: true

    width: Theme.defaultWindowWidth
    height: Theme.defaultWindowHeight
    minimumWidth: Theme.minWindowWidth
    minimumHeight: Theme.minWindowHeight
    visible: initialVisible
    title: "字录"
    color: Theme.bgApp
    flags: Qt.Window | Qt.FramelessWindowHint

    // Old-UI compatibility (main.cpp QmlBridge + AutoTest). Not used by new-UI controls.
    signal startRecording
    signal stopRecording
    signal pauseRecording
    signal readyRecording
    signal openScreen
    signal openCamera
    signal openSystemAudio
    signal openMicrophone

    // AutoTest compatibility: 0 = record, 1 = settings, 2 = library (same indices as Main.qml).
    function handleSettingItemClick(index, title) {
        console.log("[Ui] handleSettingItemClick", index, title)
        Navigation.open(index === 1 ? "settings" : index === 2 ? "library" : "record")
    }

    readonly property bool narrow: width < Theme.narrowBreakpoint

    // Alt+F4 / taskbar close: never close natively; WindowController decides (batch 1).
    onClosing: (close) => {
        close.accepted = false
        ShellView.requestClose()
    }

    // TODO(U4): restore AppPreferences.windowGeometry/windowMaximized; save on change (debounced).
    // TODO(U4): F9 / F10 / Ctrl+1/2/3 Shortcuts (window-local, §3.2.3, R9).
    // TODO(U4): T1/T4/T7 and afterRecordAction on RecorderView.recordingSaved/Failed,
    //           ShellView.startRejected -> Toast, ShellView.pageRequested -> Navigation.open.
    Connections {
        target: ShellView
        function onPageRequested(page: string) { Navigation.open(page) }
        function onSettingsPageRequested() { Navigation.open("settings/general") }
    }

    TitleBar {
        id: titleBar
        window: appWindow
        compact: appWindow.narrow
        anchors.left: parent.left
        anchors.right: parent.right
        onMinimizeClicked: ShellView.requestMinimize()
        onMaximizeToggled: appWindow.visibility === Window.Maximized ? appWindow.showNormal() : appWindow.showMaximized()
        onCloseClicked: ShellView.requestClose()
    }

    NavRail {
        id: navRail
        collapsed: appWindow.narrow
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
    }

    StackLayout {
        id: pages
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: navRail.right
        anchors.right: parent.right
        currentIndex: Navigation.page === "library" ? 1 : Navigation.page === "settings" ? 2 : 0

        RecordPage {}
        LibraryPage {}
        SettingsPage {}
    }

    ToastHost {
        anchors.fill: pages
    }

    // Dialog hosts fill the window so AppDialog centres on it.
    ShellDialogs {
        anchors.fill: parent
    }
    ContentDialogs {
        anchors.fill: parent
    }

    FloatingBar {}

    WindowResizeFrame {
        anchors.fill: parent
        window: appWindow
    }
}

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
        Navigation.open(index === 1 ? "settings" : index === 2 ? "library" : "record")
    }

    readonly property bool narrow: width < Theme.narrowBreakpoint

    // ---- R3: restore / remember window geometry ----
    // normalGeometry() is not exposed to QML, so the last Windowed-mode rect is tracked
    // here; maximized saves only flip the flag.
    property rect lastNormalGeometry: Qt.rect(x, y, width, height)

    Component.onCompleted: {
        const saved = AppPreferences.clampedGeometry(AppPreferences.windowGeometry)
        if (saved.width >= Theme.minWindowWidth && saved.height >= Theme.minWindowHeight) {
            appWindow.x = saved.x
            appWindow.y = saved.y
            appWindow.width = saved.width
            appWindow.height = saved.height
        }
        appWindow.lastNormalGeometry = Qt.rect(appWindow.x, appWindow.y, appWindow.width, appWindow.height)
        if (AppPreferences.windowMaximized)
            appWindow.showMaximized()
    }

    // Save debounced (>= 500 ms after the last change) and on teardown; only the normal
    // (non-maximized) geometry is remembered.
    Timer {
        id: geometrySave
        interval: 500
        onTriggered: appWindow.persistGeometry()
    }
    onXChanged: appWindow.scheduleGeometrySave()
    onYChanged: appWindow.scheduleGeometrySave()
    onWidthChanged: appWindow.scheduleGeometrySave()
    onHeightChanged: appWindow.scheduleGeometrySave()
    onVisibilityChanged: {
        if (appWindow.visibility === Window.Windowed)
            appWindow.lastNormalGeometry = Qt.rect(appWindow.x, appWindow.y, appWindow.width, appWindow.height)
        appWindow.scheduleGeometrySave()
    }
    Component.onDestruction: appWindow.persistGeometry()

    function scheduleGeometrySave() {
        if (visible)
            geometrySave.restart()
    }

    function persistGeometry() {
        const maximized = appWindow.visibility === Window.Maximized
        const rect = maximized ? appWindow.lastNormalGeometry
                               : Qt.rect(appWindow.x, appWindow.y, appWindow.width, appWindow.height)
        if (!maximized)
            appWindow.lastNormalGeometry = rect
        AppPreferences.saveWindowState(rect, maximized)
    }

    // ---- R9: window-local shortcuts (F9 / F10 / Ctrl+1/2/3) ----
    Shortcut {
        sequence: "F9"
        context: Qt.WindowShortcut
        onActivated: ShellView.requestStartStop()
    }
    Shortcut {
        sequence: "F10"
        context: Qt.WindowShortcut
        onActivated: ShellView.requestPauseResume()
    }
    Shortcut {
        sequence: "Ctrl+1"
        context: Qt.WindowShortcut
        onActivated: Navigation.open("record")
    }
    Shortcut {
        sequence: "Ctrl+2"
        context: Qt.WindowShortcut
        onActivated: Navigation.open("library")
    }
    Shortcut {
        sequence: "Ctrl+3"
        context: Qt.WindowShortcut
        onActivated: Navigation.open("settings")
    }

    // Alt+F4 / taskbar close: never close natively; WindowController decides (batch 1).
    onClosing: (close) => {
        close.accepted = false
        ShellView.requestClose()
    }

    // ---- shell wiring ----
    Connections {
        target: ShellView
        function onPageRequested(page: string) { Navigation.open(page) }
        function onSettingsPageRequested() { Navigation.open("settings/general") }
        // A refused start (no device / engine rejected): visible window gets a Toast.
        function onStartRejected(reason: string) {
            Toasts.show("warning", qsTr("无法开始录制"), reason, [], 0)
        }
    }

    // ---- T1 / after-record action (R8) ----
    Connections {
        target: RecorderView
        function onRecordingSaved(filePath: string, durationText: string) {
            appWindow.lastSavedFile = filePath
            const name = filePath.length > 0
                ? filePath.substring(Math.max(filePath.lastIndexOf("/"), filePath.lastIndexOf("\\")) + 1)
                : ""
            // §3.7: T1 fires whatever the after-record setting is.
            Toasts.show("success", qsTr("录像已保存"),
                        name + " · " + durationText,
                        [{ id: "t1.openFolder", text: qsTr("打开文件夹") },
                         { id: "t1.play", text: qsTr("播放"), ghost: true }], 0)
            const action = AppPreferences.afterRecordAction
            if (action === "openFolder")
                DesktopActions.openFolder(filePath)
            else if (action === "library")
                Navigation.open("library")
        }
        // §3.2.4 rule 2: recording started with subtitles enabled but not ready -> T4.
        function onUiStateChanged() {
            if (RecorderView.uiState !== "preparing" || !SubtitleView.serviceAttached)
                return
            if (!SubtitleView.enabled)
                return
            const sub = SubtitleView.uiState
            if (sub === "needModel" || sub === "downloading" || sub === "unavailable") {
                const reason = sub === "unavailable" ? SubtitleView.lastError
                             : qsTr("还没有下载识别模型")
                Toasts.show("warning", qsTr("本次录制不会生成字幕"), reason,
                            [{ id: "t4.download", text: qsTr("去下载") }], 0)
            }
        }
    }

    Connections {
        target: Toasts
        function onActionTriggered(actionId: string) {
            if (actionId === "t1.openFolder" && appWindow.lastSavedFile.length > 0)
                DesktopActions.openFolder(appWindow.lastSavedFile)
            else if (actionId === "t1.play" && appWindow.lastSavedFile.length > 0)
                DesktopActions.openFile(appWindow.lastSavedFile)
            else if (actionId === "t4.download")
                Navigation.open("settings/subtitle")
        }
    }
    property string lastSavedFile: ""

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

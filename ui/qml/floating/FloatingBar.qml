import QtQuick
import QtQuick.Window

// Module FB2 (ZCode). Mini floating bar, 264×40 capsule in its own top-level window
// (MINIMIZE_DESIGN §3.6, §4.5, §6.3). The window grows to 136 high while the FB-H hint shows.
// FROZEN contract (see FloatingBarView): created once by AppWindow with visible:false;
// Component.onCompleted calls FloatingBarView.attachWindow(root) BEFORE any show;
// `visible` follows FloatingBarView.shown; position from FloatingBarView.preferredPosition;
// drag via startSystemMove, release -> FloatingBarView.savePosition.
// Buttons: pause/resume -> FloatingBarView.pauseResume(); stop -> stop(); open main
// window -> openMainWindow(); FB-6 close -> dismiss(). Tooltips use
// HotkeyController.shortcutLabel(...). 60 % opacity 3 s after the mouse leaves (FB-D).
Window {
    id: root

    width: 264
    height: FloatingBarView.hintVisible ? 136 : 40
    visible: false
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    title: "字录 悬浮条"

    Component.onCompleted: FloatingBarView.attachWindow(root)

    // TODO(FB2): show/hide on FloatingBarView.shown (position first), states FB-1..FB-6,
    // FB-D fade, FB-H bubble, drag + snap.
    Rectangle {
        width: 264
        height: 40
        radius: 20
        color: Theme.bgSurface
        border.width: 1
        border.color: Theme.borderDefault
    }
}

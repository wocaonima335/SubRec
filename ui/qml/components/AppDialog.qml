import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.11 modal dialog inside the main window (never a separate window,
// so capture exclusion covers it automatically).
// FROZEN API:
//   title, dialogWidth (440 confirm / 560 licence)
//   primaryText, primaryKind ("primary" | "danger"), primaryEnabled
//   secondaryText ("" hides), showCloseButton (top-right 32×32, same as cancel)
//   default property `content`: body between title (+12) and buttons (+24)
//   signals accepted() (primary), rejected() (secondary / close icon / Esc)
//   Enter triggers primary unless primaryKind === "danger".
// Open/close with open()/close() as any Popup.
Popup {
    id: root
    property string title: ""
    property int dialogWidth: 440
    property string primaryText: "确定"
    property string primaryKind: "primary"
    property bool primaryEnabled: true
    property string secondaryText: "取消"
    property bool showCloseButton: true
    default property alias content: body.data
    signal accepted()
    signal rejected()

    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    width: dialogWidth
    // Hosts (ShellDialogs / ContentDialogs) fill the window, so `parent` centres on it.
    anchors.centerIn: parent
    padding: Theme.space24

    // TODO(U2): title, close icon, button row, Esc/Enter handling, scrim (Overlay.modal),
    // 150 ms opacity+scale enter animation.
    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.bgSurface
        border.width: 1
        border.color: Theme.borderDefault
    }
    contentItem: Item {
        id: body
        implicitHeight: childrenRect.height
    }
}

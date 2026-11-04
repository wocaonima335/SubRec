import QtQuick

// Module U2 (ZCode). §2.5.10 inline alert.
// FROZEN API:
//   kind: "info" | "success" | "warning" | "danger"
//   title, body (max 3 lines), actionText ("" = no button), closable
//   signals actionClicked(), closed()
Rectangle {
    id: root
    property string kind: "info"
    property string title: ""
    property string body: ""
    property string actionText: ""
    property bool closable: false
    signal actionClicked()
    signal closed()

    radius: Theme.radiusMd
    implicitHeight: 56
    color: kind === "danger" ? Theme.dangerBg
         : kind === "warning" ? Theme.warningBg
         : kind === "success" ? Theme.successBg : Theme.infoBg

    // TODO(U2): icon, title/body texts, small action button, close icon button, border.
}

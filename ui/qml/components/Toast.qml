import QtQuick

// Module U2 (ZCode). One toast, 360 wide (§3.7 layout: icon, title, close, body, actions).
// FROZEN API: kind, title, body, actions (as Toasts.show); signals actionClicked(id), dismissed().
Rectangle {
    id: root
    property string kind: "info"
    property string title: ""
    property string body: ""
    property var actions: []
    signal actionClicked(string actionId)
    signal dismissed()

    width: 360
    implicitHeight: actions.length > 0 ? 104 : body.length > 0 ? 70 : 52
    radius: Theme.radiusMd
    color: Theme.bgSurface
    border.width: 1
    border.color: Theme.borderDefault

    // TODO(U2): content, shadow/popup.
}

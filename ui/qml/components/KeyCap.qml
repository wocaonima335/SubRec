import QtQuick

// Module H3 (Codex). Key cap for the read-only shortcut list (§6.4.3): 24 high, padding 8,
// radius 4, bg/surface-2, 1 px border, `type/mono-sm`.
// FROZEN API: text.
Rectangle {
    id: root
    property string text: ""

    implicitHeight: 24
    implicitWidth: label.implicitWidth + 16
    radius: Theme.radiusXs
    color: Theme.bgSurface2
    border.width: 1
    border.color: Theme.borderDefault

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        font: Theme.monoSmallFont
        color: Theme.textPrimary
    }
}

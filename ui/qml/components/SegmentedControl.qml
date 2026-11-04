import QtQuick

// Module U2 (ZCode). §2.5.5 segmented control.
// FROZEN API:
//   model: JS array of { text, value, iconName? }
//   currentValue: value of the selected segment (bind from the backend, never set from inside)
//   locked: §3.2.5; disabledReason
//   signal activated(var value): user picked a segment (owner writes the backend)
Item {
    id: root
    property var model: []
    property var currentValue
    property bool locked: false
    property string disabledReason: ""
    signal activated(var value)

    implicitHeight: Theme.controlHeight
    implicitWidth: 240

    // TODO(U2): container + equal-width segments, selected style per theme.
    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusMd
        color: Theme.bgSurface2
    }
}

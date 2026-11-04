import QtQuick

// Module U2 (ZCode). §2.5.11 card: bg/surface, 1 px border/default, radius 8, padding 16,
// optional 16 SemiBold title with 12 px gap. No shadow.
// FROZEN API: title, padding, default property `content` (children go into the body).
Rectangle {
    id: root
    property string title: ""
    property int padding: Theme.space16
    default property alias content: body.data

    color: Theme.bgSurface
    radius: Theme.radiusMd
    border.width: 1
    border.color: Theme.borderDefault

    // TODO(U2): title row.
    Item {
        id: body
        anchors.fill: parent
        anchors.margins: root.padding
    }
}

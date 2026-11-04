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

    Text {
        id: titleText
        visible: root.title.length > 0
        text: root.title
        font: Theme.h2Font
        color: Theme.textPrimary
        anchors.top: parent.top
        anchors.topMargin: root.padding
        anchors.left: parent.left
        anchors.leftMargin: root.padding
        anchors.right: parent.right
        anchors.rightMargin: root.padding
    }

    Item {
        id: body
        anchors.top: root.title.length > 0 ? titleText.bottom : parent.top
        anchors.topMargin: root.title.length > 0 ? Theme.space12 : root.padding
        anchors.left: parent.left
        anchors.leftMargin: root.padding
        anchors.right: parent.right
        anchors.rightMargin: root.padding
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.padding
    }
}

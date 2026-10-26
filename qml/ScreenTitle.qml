import QtQuick 2.15

Item {
    id: root
    // 对外暴露的标题文本
    property string title: "1080 X 720 : Screen"

    // 高度与布局
    height: 20

    Text {
        id: titleText
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 10
        horizontalAlignment: Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        text: root.title
        color: "white"
        font.pixelSize: 12
        elide: Text.ElideMiddle
    }
}

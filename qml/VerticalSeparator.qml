import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root
    
    width: 1
    color: "#40454F"
    opacity: 0.9
    
    // 高度默认跟随父容器，留出一点边距
    anchors.top: parent.top
    anchors.bottom: parent.bottom
    anchors.margins: 5
}

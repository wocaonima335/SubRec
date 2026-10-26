import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

Item {
    id: root
    
    // 信号与属性
    signal itemClicked(int index, string title)
    property int currentIndex: 0

    // 数据模型
    ListModel {
        id: settingList
        ListElement {
            title: "主页"
            icon: "icons/main.png"
            iconGray: "icons/main-gray.png"
        }
        ListElement {
            title: "设置"
            icon: "icons/Setting.png"
            iconGray: "icons/setting-gray.png"
        }
        ListElement {
            title: "视频"
            icon: "icons/video.png"
            iconGray: "icons/video-gray.png"
        }
    }

    ListView {
        id: listView
        anchors.fill: parent
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        spacing: 4
        
        model: settingList
        currentIndex: root.currentIndex
        
        // 禁用默认的高亮跟随（我们自己处理点击，但保持高亮组件用于动画）
        highlightFollowsCurrentItem: true
        highlightMoveDuration: 250
        highlightMoveVelocity: -1 // 速度为-1表示由 duration 控制

        // 平滑滑动的选中背景 (Highlighter)
        highlight: Item {
            width: listView.width
            height: 48 
            
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                radius: 6
                color: "#353A45" // 选中项背景色
                
                // 左侧蓝色指示条
                Rectangle {
                    width: 3
                    height: 20
                    radius: 1.5
                    color: "#4A9EFF"
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                }

                // 简单的选中发光
                layer.enabled: true
                // 注意：如果想用效果需要 import QtQuick.Effects，这里保持简单或确定已引入
            }
        }

        delegate: Item {
            id: delegateItem
            width: listView.width
            height: 48 // 增加一点高度，更宽松
            
            required property string icon
            required property string iconGray
            required property string title
            required property int index

            property bool isSelected: ListView.isCurrentItem
            property bool isHovered: mouseArea.containsMouse

            // 悬停背景 (非选中时)
            Rectangle {
                anchors.fill: parent
                anchors.margins: 4
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                radius: 6
                color: "#2C3039" // 略微亮一点的悬停色
                opacity: (delegateItem.isHovered && !delegateItem.isSelected) ? 1 : 0
                
                Behavior on opacity { NumberAnimation { duration: 150 } }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24 // 留出左侧指示条的空间
                anchors.rightMargin: 16
                spacing: 12
                
                // 图标
                Image {
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    source: delegateItem.isSelected ? delegateItem.icon : delegateItem.iconGray
                    fillMode: Image.PreserveAspectFit
                    opacity: delegateItem.isSelected ? 1.0 : 0.7
                    
                    Behavior on opacity { NumberAnimation { duration: 200 } }
                }

                // 标题
                Text {
                    Layout.fillWidth: true
                    text: delegateItem.title
                    color: delegateItem.isSelected ? "#FFFFFF" : "#A0A5B0"
                    font.pixelSize: 15
                    font.bold: delegateItem.isSelected
                    font.family: "Microsoft YaHei" // 或系统默认
                    
                    Behavior on color { ColorAnimation { duration: 200 } }
                }
            }

            MouseArea {
                id: mouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    listView.currentIndex = delegateItem.index
                    root.currentIndex = delegateItem.index
                    root.itemClicked(delegateItem.index, delegateItem.title)
                }
            }
        }
    }
}

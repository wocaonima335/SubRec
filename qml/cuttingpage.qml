import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.folderlistmodel 2.15

Item {
    id: cuttingpage
    property bool showPreview: false


    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        // Header Title
        Text {
            text: "VIDEO LIST"
            font.pixelSize: 22
            font.bold: true
            font.weight: Font.DemiBold
            color: "#FFFFFF"
            font.family: "Segoe UI"
            opacity: 0.95
        }

        // 分隔线 - 微调颜色
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#FFFFFF"
            opacity: 0.1
        }

        // 视频列表区域 - 优化边框和背景
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#202227" // 更深沉的背景
            radius: 8
            border.color: "#FFFFFF"
            border.width: 1
            opacity: 1
            // 边框透明度单独控制比较麻烦，这里用 border.color 的 alpha
            Rectangle {
                anchors.fill: parent
                radius: 8
                border.color: "#FFFFFF"
                border.width: 1
                color: "transparent"
                opacity: 0.08
            }
            clip: true

            ListView {
                id: videoListView
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                spacing: 4

                model: FolderListModel {
                    id: folderModel
                    folder: {
                        var path = recorder.outputPath;
                        if (path.indexOf(":") === 1) {
                             return "file:///" + path;
                        }
                        return "file://" + path;
                    }
                    nameFilters: ["*.mp4", "*.avi", "*.mkv", "*.flv", "*.mov"]
                    showDirs: false
                }

                delegate: Rectangle {
                    width: videoListView.width
                    height: 48 // 增加行高
                    color: mouseArea.containsMouse || videoListView.currentIndex === index ? "#323642" : "transparent"
                    radius: 6

                    // 选中状态的高亮边框
                    border.color: videoListView.currentIndex === index ? "#00A2E8" : "transparent"
                    border.width: 1

                    Behavior on color { ColorAnimation { duration: 150 } }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 12

                        // 图标容器
                        Rectangle {
                            width: 32
                            height: 32
                            radius: 6
                            color: "#40454F"
                            Image {
                                anchors.centerIn: parent
                                source: "icons/video-gray.png"
                                width: 20
                                height: 20
                                fillMode: Image.PreserveAspectFit
                                opacity: 0.9
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            
                            Text {
                                text: fileName
                                color: "#E0E0E0"
                                font.pixelSize: 14
                                font.bold: true
                                font.family: "Segoe UI"
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            
                            // 显示文件大小或日期会更好，这里暂时显示部分路径作为次要信息
                            Text {
                                text: filePath
                                color: "#808590"
                                font.pixelSize: 11
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                opacity: 0.7
                            }
                        }
                    }

                    MouseArea {
                        id: mouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: videoListView.currentIndex = index
                        onDoubleClicked: {
                            Qt.openUrlExternally("file:///" + filePath)
                        }
                    }
                }

                ScrollIndicator.vertical: ScrollIndicator { 
                    parent: videoListView.parent // 滚动条依附于父容器边缘
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                }

                // 无视频时的占位提示
                Item {
                    anchors.centerIn: parent
                    visible: folderModel.count === 0
                    Column {
                        spacing: 10
                        anchors.centerIn: parent
                        
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "No Videos Found"
                            color: "#FFFFFF"
                            font.pixelSize: 16
                            font.bold: true
                            opacity: 0.6
                        }
                        
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Output Path: " + recorder.outputPath
                            color: "#808590"
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }

        // 底部工具栏
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            
            Button { 
                text: "Refresh"
                background: Rectangle {
                    color: parent.down ? "#3A4050" : (parent.hovered ? "#323642" : "#2B2E36")
                    radius: 4
                    border.color: "#3A4050"
                }
                contentItem: Text {
                    text: parent.text
                    color: "#E0E0E0"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: folderModel.folder = folderModel.folder
            }
            
            Item { Layout.fillWidth: true }
            
            Button { 
                text: "Open Folder" 
                background: Rectangle {
                    color: parent.down ? "#0081B8" : (parent.hovered ? "#0092D0" : "#00A2E8")
                    radius: 4
                }
                contentItem: Text {
                    text: parent.text
                    color: "#FFFFFF"
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: Qt.openUrlExternally(folderModel.folder) 
            }
        }
    }
}

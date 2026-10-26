import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.platform 1.1

Item {
    id: settingpage
    property bool showPreview: false

    FolderDialog {
        id: folderDialog
        title: "Select Output Folder"
        folder: StandardPaths.writableLocation(StandardPaths.MoviesLocation)
        onAccepted: {
            var path = folderDialog.folder.toString()
            if (path.startsWith("file:///")) {
                path = path.substring(8)
            }
            recorder.outputPath = path
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Text {
            text: "SETTINGS"
            font.pixelSize: 22
            font.bold: true
            font.weight: Font.DemiBold
            color: "#FFFFFF"
            font.family: "Segoe UI"
            opacity: 0.95
        }

        // 分隔线
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#FFFFFF"
            opacity: 0.1
        }

        // 设置项容器 (可滚动但无滚动条)
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: settingsColumn.height
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: settingsColumn
                width: parent.width
                spacing: 20

                // 选项 1
                RowLayout {
                    spacing: 12
                    Text {
                        text: "Start recording on launch"
                        color: "#E0E0E0"
                        font.pixelSize: 14
                        Layout.fillWidth: true
                    }
                    Switch {
                        checked: false
                        // 自定义 Switch 样式
                        indicator: Rectangle {
                            implicitWidth: 40
                            implicitHeight: 20
                            x: parent.leftPadding
                            y: parent.height / 2 - height / 2
                            radius: 10
                            color: parent.checked ? "#00A2E8" : "#3A4050"
                            border.color: parent.checked ? "#00A2E8" : "#999999"
                            border.width: 0

                            Rectangle {
                                x: parent.parent.checked ? parent.width - width - 2 : 2
                                y: 2
                                width: 16
                                height: 16
                                radius: 8
                                color: "#ffffff"
                                Behavior on x {
                                    NumberAnimation {
                                        duration: 150
                                    }
                                }
                            }
                        }
                    }
                }

                // 选项 2
                RowLayout {
                    spacing: 12
                    Text {
                        text: "Show FPS overlay"
                        color: "#E0E0E0"
                        font.pixelSize: 14
                        Layout.fillWidth: true
                    }
                    Switch {
                        checked: true
                        indicator: Rectangle {
                            implicitWidth: 40
                            implicitHeight: 20
                            x: parent.leftPadding
                            y: parent.height / 2 - height / 2
                            radius: 10
                            color: parent.checked ? "#00A2E8" : "#3A4050"
                            border.color: parent.checked ? "#00A2E8" : "#999999"
                            border.width: 0

                            Rectangle {
                                x: parent.parent.checked ? parent.width - width - 2 : 2
                                y: 2
                                width: 16
                                height: 16
                                radius: 8
                                color: "#ffffff"
                                Behavior on x {
                                    NumberAnimation {
                                        duration: 150
                                    }
                                }
                            }
                        }
                    }
                }

                // 选项 3
                RowLayout {
                    spacing: 12
                    Text {
                        text: "Output Format"
                        color: "#E0E0E0"
                        font.pixelSize: 14
                    }
                    Item {
                        Layout.fillWidth: true
                    } // Spacer
                    ComboBox {
                        model: ["MP4", "AVI", "MKV"]
                        Layout.preferredWidth: 100
                        Layout.preferredHeight: 32

                        background: Rectangle {
                            color: parent.down ? "#252830" : "#2B2E36"
                            border.color: parent.activeFocus ? "#00A2E8" : "#3A4050"
                            radius: 4
                        }
                        contentItem: Text {
                            leftPadding: 10
                            rightPadding: parent.indicator.width + 10
                            text: parent.displayText
                            font: parent.font
                            color: "#E0E0E0"
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                }

                // 分割线
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: "#FFFFFF"
                    opacity: 0.05
                    Layout.topMargin: 4
                    Layout.bottomMargin: 4
                }

                ColumnLayout {
                    spacing: 8

                    Text {
                        text: "Capture Devices"
                        color: "#FFFFFF"
                        font.pixelSize: 14
                        font.bold: true
                        opacity: 0.8
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "DirectShow devices: " + recorder.captureDevices.length
                            color: recorder.captureDevices.length > 0 ? "#A8DDA8" : "#F0B1B1"
                            font.pixelSize: 12
                            Layout.fillWidth: true
                        }
                        Button {
                            text: "Refresh"
                            onClicked: recorder.refreshCaptureDevices()
                        }
                    }

                    Text {
                        visible: recorder.captureDevices.length === 0
                        text: "No DirectShow capture devices were found. Install or select a supported input before recording."
                        color: "#D99B9B"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }

                    ComboBox {
                        id: videoDeviceBox
                        Layout.fillWidth: true
                        textRole: "displayName"
                        valueRole: "id"
                        model: recorder.captureDevices.filter(function(device) {
                            return device.kind === "screen" || device.kind === "camera"
                        })
                        currentIndex: model.findIndex(function(device) {
                            return device.id === recorder.selectedVideoDeviceId
                        })
                        onActivated: recorder.selectVideoCaptureDevice(currentValue)
                    }

                    ComboBox {
                        id: audioDeviceBox
                        Layout.fillWidth: true
                        textRole: "displayName"
                        valueRole: "id"
                        model: recorder.captureDevices.filter(function(device) {
                            return device.kind === "system-audio" || device.kind === "microphone"
                        })
                        currentIndex: model.findIndex(function(device) {
                            return device.id === recorder.selectedAudioDeviceId
                        })
                        onActivated: recorder.selectAudioCaptureDevice(currentValue)
                    }
                }

                // 输出路径配置
                ColumnLayout {
                    spacing: 8

                    Text {
                        text: "Output Settings"
                        color: "#FFFFFF"
                        font.pixelSize: 14
                        font.bold: true
                        opacity: 0.8
                    }

                    ColumnLayout {
                        spacing: 4
                        Layout.leftMargin: 4

                        Text {
                            text: "Output Path"
                            color: "#808590"
                            font.pixelSize: 12
                        }
                        RowLayout {
                            spacing: 8
                            TextField {
                                id: pathField
                                text: recorder.outputPath
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                selectByMouse: true
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: 8
                                onEditingFinished: recorder.outputPath = text
                                background: Rectangle {
                                    color: "#1A1D24"
                                    border.color: pathField.activeFocus ? "#00A2E8" : "#3A4050"
                                    radius: 4
                                }
                                color: "#E0E0E0"
                                selectionColor: "#00A2E8"
                            }
                            Button {
                                text: "Browse"
                                Layout.preferredHeight: 32
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
                                onClicked: folderDialog.open()
                            }
                        }
                    }

                    ColumnLayout {
                        spacing: 4
                        Layout.leftMargin: 4

                        Text {
                            text: "Filename"
                            color: "#808590"
                            font.pixelSize: 12
                        }
                        TextField {
                            id: fileField
                            text: recorder.outputFileName
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            selectByMouse: true
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 8
                            onEditingFinished: recorder.outputFileName = text
                            background: Rectangle {
                                color: "#1A1D24"
                                border.color: fileField.activeFocus ? "#00A2E8" : "#3A4050"
                                radius: 4
                            }
                            color: "#E0E0E0"
                            selectionColor: "#00A2E8"
                        }
                    }
                }
            }
        }
    }
}

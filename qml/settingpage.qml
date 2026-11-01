import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.platform 1.1

Item {
    id: settingpage
    property bool showPreview: false

    // Shell settings row (switch). Same switch style as the rows above.
    component ShellSwitchRow: RowLayout {
        id: row
        property string title: ""
        property string note: ""
        property bool checked: false
        property string disabledTip: ""
        signal toggled(bool on)
        spacing: 12
        Layout.fillWidth: true
        opacity: enabled ? 1.0 : 0.4
        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true
            Text { text: row.title; color: "#E0E0E0"; font.pixelSize: 14 }
            Text { text: row.note; color: "#808590"; font.pixelSize: 12; visible: text.length > 0
                   wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
        Switch {
            id: sw
            checked: row.checked
            onToggled: row.toggled(checked)
            ToolTip.visible: !row.enabled && row.disabledTip.length > 0 && hovered
            ToolTip.text: row.disabledTip
            indicator: Rectangle {
                implicitWidth: 40
                implicitHeight: 20
                x: sw.leftPadding
                y: sw.height / 2 - height / 2
                radius: 10
                color: sw.checked ? "#00A2E8" : "#3A4050"
                Rectangle {
                    x: sw.checked ? parent.width - width - 2 : 2
                    y: 2; width: 16; height: 16; radius: 8; color: "#ffffff"
                    Behavior on x { NumberAnimation { duration: 150 } }
                }
            }
        }
    }

    component ShellComboRow: RowLayout {
        id: crow
        property string title: ""
        property string note: ""
        property color noteColor: "#808590"
        property var options: []       // [{ text, value }]
        property string value: ""
        signal picked(string value)
        spacing: 12
        Layout.fillWidth: true
        opacity: enabled ? 1.0 : 0.4
        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true
            Text { text: crow.title; color: "#E0E0E0"; font.pixelSize: 14 }
            Text { text: crow.note; color: crow.noteColor; font.pixelSize: 12; visible: text.length > 0
                   wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
        ComboBox {
            Layout.preferredWidth: 200
            textRole: "text"
            valueRole: "value"
            model: crow.options
            currentIndex: Math.max(0, indexOfValue(crow.value))
            onActivated: crow.picked(currentValue)
        }
    }

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
                            text: "Capture devices: " + recorder.captureDevices.length
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

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "Screen backend"
                            color: "#FFFFFF"
                            font.pixelSize: 12
                            opacity: 0.8
                        }
                        ComboBox {
                            id: screenBackendBox
                            Layout.fillWidth: true
                            // SUBREC_CAPTURE_BACKEND overrides the setting; no change while recording.
                            enabled: !recorder.screenCaptureBackendFromEnvironment && recorder.canStartCapture
                            textRole: "text"
                            valueRole: "value"
                            model: [
                                { value: "auto", text: "Auto (Desktop Duplication if available)" },
                                { value: "ddagrab", text: "Desktop Duplication (ddagrab)" },
                                { value: "dshow", text: "DirectShow (screen-capture-recorder)" }
                            ]
                            currentIndex: Math.max(0, model.findIndex(function(item) {
                                return item.value === recorder.screenCaptureBackend
                            }))
                            onActivated: recorder.setScreenCaptureBackend(currentValue)
                        }
                    }

                    Text {
                        visible: recorder.screenCaptureBackendFromEnvironment
                        text: "Screen backend is set by SUBREC_CAPTURE_BACKEND=" + recorder.screenCaptureBackend
                        color: "#D9C99B"
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
                        enabled: recorder.canStartCapture
                        Layout.fillWidth: true
                        textRole: "displayName"
                        valueRole: "id"
                        model: recorder.captureDevices.filter(function(device) {
                            return device.available && device.kind === (recorder.audioMode === "microphone"
                                   ? "microphone" : "system-audio")
                        })
                        currentIndex: model.findIndex(function(device) {
                            return device.id === recorder.selectedAudioDeviceId
                        })
                        onActivated: recorder.selectAudioCaptureDevice(currentValue)
                    }

                    Text { text: "Microphone (single or mixed)"; color: "#B0B0B0" }
                    ComboBox {
                        Layout.fillWidth: true
                        enabled: recorder.canStartCapture
                        textRole: "displayName"
                        valueRole: "id"
                        model: recorder.captureDevices.filter(function(device) {
                            return device.kind === "microphone" && device.available
                        })
                        currentIndex: model.findIndex(function(device) {
                            return device.id === recorder.selectedMicrophoneDeviceId
                        })
                        onActivated: recorder.selectMicrophoneCaptureDevice(currentValue)
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

                // 窗口与托盘 (MINIMIZE_DESIGN.md §6.4.2, C1-C4, C7)
                ColumnLayout {
                    spacing: 12
                    Layout.fillWidth: true
                    readonly property string trayTip: "需要先打开“在通知区域显示图标”"

                    Text { text: "窗口与托盘"; color: "#FFFFFF"; font.pixelSize: 14; font.bold: true; opacity: 0.8 }

                    ShellSwitchRow {
                        title: "在通知区域显示图标"
                        note: "关闭后，下面三项不可用"
                        checked: shellSettings.trayEnabled
                        onToggled: (on) => shellSettings.trayEnabled = on
                    }
                    ShellSwitchRow {
                        title: "最小化时隐藏到托盘"
                        note: "不在任务栏留按钮，点托盘图标恢复"
                        enabled: shellSettings.trayEnabled
                        disabledTip: parent.trayTip
                        checked: shellSettings.minimizeToTray
                        onToggled: (on) => shellSettings.minimizeToTray = on
                    }
                    ShellComboRow {
                        title: "点关闭按钮时"
                        note: "选“最小化到托盘”时，关闭窗口不会停止录制"
                        enabled: shellSettings.trayEnabled
                        options: [ { text: "每次询问", value: "ask" },
                                   { text: "最小化到托盘", value: "tray" },
                                   { text: "退出字录", value: "exit" } ]
                        // C1 off: shown as "退出字录" (§6.4.2 state table).
                        value: shellSettings.trayEnabled ? shellSettings.closeAction : "exit"
                        onPicked: (v) => shellSettings.closeAction = v
                    }
                    ShellSwitchRow {
                        title: "启动时最小化到托盘"
                        note: "也可以用命令行参数 --minimized"
                        enabled: shellSettings.trayEnabled
                        disabledTip: parent.trayTip
                        checked: shellSettings.startMinimized
                        onToggled: (on) => shellSettings.startMinimized = on
                    }
                    ShellSwitchRow {
                        title: "开始录制后自动隐藏主窗口"
                        note: "隐藏后可以用托盘控制"
                        checked: shellSettings.hideOnRecordStart
                        onToggled: (on) => shellSettings.hideOnRecordStart = on
                    }
                }

                // 录像画面 (C6)
                ColumnLayout {
                    spacing: 12
                    Layout.fillWidth: true

                    Text { text: "录像画面"; color: "#FFFFFF"; font.pixelSize: 14; font.bold: true; opacity: 0.8 }

                    ShellComboRow {
                        title: "不把字录窗口录进画面"
                        enabled: shellSettings.excludeSupported
                        noteColor: shellSettings.excludeSupported ? "#808590" : "#E8B04A"
                        note: shellSettings.excludeSupported
                              ? "开启后，截图工具、会议共享、远程协助也看不到字录窗口。仅对‘桌面复制’采集方式保证有效。"
                              : "需要 Windows 10 2004 或更高版本。当前系统上字录窗口会被录进画面，可以打开上面的‘开始录制后自动隐藏主窗口’。"
                        options: shellSettings.excludeSupported
                                 ? [ { text: "仅录制时", value: "recording" },
                                     { text: "始终", value: "always" },
                                     { text: "从不", value: "never" } ]
                                 : [ { text: "不支持", value: "never" } ]
                        value: shellSettings.excludeSupported ? shellSettings.excludeFromCapture : "never"
                        onPicked: (v) => shellSettings.excludeFromCapture = v
                    }
                }

                // 退出确认
                ShellSwitchRow {
                    title: "退出字录时，如正在录制先确认"
                    note: "退出会停止录制并保存已录内容"
                    checked: shellSettings.confirmExitWhileRecording
                    onToggled: (on) => shellSettings.confirmExitWhileRecording = on
                }
            }
        }
    }
}

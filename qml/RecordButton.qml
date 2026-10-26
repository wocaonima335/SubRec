import QtQuick 2.15
import QtQuick.Effects
import QtQuick.Controls 2.15

Item {
    id: root
    
    // 属性和信号
    property var recorderObject: null
    property bool isRecording: recorderObject ? recorderObject.isRecording : false

    signal startClicked()
    signal stopClicked()
    signal pauseClicked()
    signal resumeClicked()

    onIsRecordingChanged: {
        if (!isRecording) {
            pauseButton.isPaused = false
        }
    }

    // 背景轨道
    Rectangle {
        id: bgTrack
        anchors.fill: parent
        anchors.margins: 12
        anchors.topMargin: 14 // 微调
        anchors.bottomMargin: 14
        radius: height / 2
        
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1A1C20" }
            GradientStop { position: 1.0; color: "#2B2E36" }
        }
        
        border.color: "#353941"
        border.width: 1
        
        // 内部阴影模拟（顶部深色）
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: parent.height / 2
            radius: parent.radius
            color: "transparent"
            border.color: "#10000000"
            border.width: 0
            visible: false
        }
    }

    // 暂停按钮 (当录制时显示，在右侧)
    Item {
        id: pauseButton
        anchors.verticalCenter: parent.verticalCenter
        x: root.width - width - 24 // 右侧位置
        width: 32
        height: 32
        z: 1

        property bool isPaused: false
        property bool isHovered: pauseMouse.containsMouse

        opacity: root.isRecording ? 1.0 : 0.0
        visible: opacity > 0
        scale: isHovered ? 1.1 : 1.0

        Behavior on opacity { NumberAnimation { duration: 300 } }
        Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: pauseButton.isHovered ? "#3A4050" : "transparent"
        }

        Image {
            id: pauseIcon
            source: "icons/Pause.png"
            anchors.centerIn: parent
            width: 20
            height: 20
            fillMode: Image.PreserveAspectFit
            opacity: pauseButton.isPaused ? 0.5 : (pauseButton.isHovered ? 1.0 : 0.8)
        }
        
        // 暂停闪烁动画
        SequentialAnimation {
            running: pauseButton.isPaused && root.isRecording
            loops: Animation.Infinite
            NumberAnimation { target: pauseIcon; property: "opacity"; from: 1; to: 0.3; duration: 600; easing.type: Easing.InOutQuad }
            NumberAnimation { target: pauseIcon; property: "opacity"; from: 0.3; to: 1; duration: 600; easing.type: Easing.InOutQuad }
        }

        MouseArea {
            id: pauseMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                pauseButton.isPaused = !pauseButton.isPaused
                if (pauseButton.isPaused) {
                    root.pauseClicked()
                } else {
                    root.resumeClicked()
                }
            }
        }
    }

    // 录制旋钮 (滑块)
    Rectangle {
        id: recordKnob
        anchors.verticalCenter: parent.verticalCenter
        width: 80
        height: 80
        radius: 40
        z: 2
        
        // 位置动画：录制时滑到左侧，非录制时在右侧
        x: root.isRecording ? 5 : root.width - width - 5
        Behavior on x {
            NumberAnimation { duration: 400; easing.type: Easing.OutBack; easing.overshoot: 0.8 }
        }

        // 旋钮背景渐变
        gradient: Gradient {
            GradientStop { 
                position: 0.0
                color: root.isRecording ? "#FF5F5F" : "#FF1A1A"
            }
            GradientStop { 
                position: 1.0
                color: root.isRecording ? "#CC0000" : "#CC0000"
            }
        }

        border.color: "#FFFFFF"
        border.width: 2

        // 悬停缩放
        scale: knobMouse.containsMouse || knobMouse.pressed ? 1.05 : 1.0
        Behavior on scale { NumberAnimation { duration: 150 } }

        // 阴影
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: root.isRecording ? "#60FF0000" : "#40000000"
            shadowBlur: root.isRecording ? 1.5 : 1.0
            shadowVerticalOffset: 2
        }

        // 录制中的呼吸光圈
        Rectangle {
            anchors.centerIn: parent
            width: parent.width + 8
            height: parent.height + 8
            radius: width / 2
            color: "transparent"
            border.color: "#FF4444"
            border.width: 2
            opacity: 0
            visible: root.isRecording
            
            SequentialAnimation on opacity {
                running: root.isRecording
                loops: Animation.Infinite
                NumberAnimation { from: 0; to: 0.5; duration: 1000; easing.type: Easing.OutSine }
                NumberAnimation { from: 0.5; to: 0; duration: 1000; easing.type: Easing.InSine }
            }
            
            SequentialAnimation on width {
                running: root.isRecording
                loops: Animation.Infinite
                NumberAnimation { from: recordKnob.width; to: recordKnob.width + 12; duration: 2000; easing.type: Easing.OutQuad }
            }
            SequentialAnimation on height {
                running: root.isRecording
                loops: Animation.Infinite
                NumberAnimation { from: recordKnob.height; to: recordKnob.height + 12; duration: 2000; easing.type: Easing.OutQuad }
            }
        }

        // 文本
        Text {
            anchors.centerIn: parent
            text: root.isRecording ? "STOP" : "REC"
            color: "#FFFFFF"
            font.pixelSize: 14
            font.bold: true
            font.letterSpacing: 1
            style: Text.Outline
            styleColor: "#40000000"
        }

        MouseArea {
            id: knobMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: root.isRecording || !root.recorderObject || root.recorderObject.canStartCapture
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
            onClicked: {
                if (root.isRecording) {
                    root.stopClicked()
                } else {
                    root.startClicked()
                }
            }
        }
    }
}

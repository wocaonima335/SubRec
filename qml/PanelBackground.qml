import QtQuick 2.15
import QtQuick.Effects

Item {
    id: root

    property bool isHovered: false
    property bool isRecording: false
    property color recordingColor: "#4A9EFF"

    Rectangle {
        id: backgroundContainer
        anchors.fill: parent
        anchors.margins: 4
        radius: 6

        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: root.isHovered ? "#353A4A" : "#2A2F3E"
            }
            GradientStop {
                position: 1.0
                color: root.isHovered ? "#2A2F3E" : "#1F242E"
            }
        }

        border.color: root.isRecording ? root.recordingColor : "#40454F"
        border.width: 1

        Behavior on border.color {
            ColorAnimation { duration: 300; easing.type: Easing.OutCubic }
        }

        layer.enabled: root.isHovered || root.isRecording
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowHorizontalOffset: 0
            shadowVerticalOffset: 2
            shadowBlur: 0.6
            shadowColor: "#40000000"
        }
    }

    Rectangle {
        id: recordingGlow
        anchors.fill: backgroundContainer
        anchors.margins: -2
        radius: backgroundContainer.radius + 2
        color: "transparent"
        border.color: root.recordingColor
        border.width: 2
        visible: root.isRecording

        SequentialAnimation on opacity {
            running: root.isRecording
            loops: Animation.Infinite
            NumberAnimation { from: 0; to: 0.6; duration: 1000; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.6; to: 0; duration: 1000; easing.type: Easing.InOutSine }
        }
    }
}

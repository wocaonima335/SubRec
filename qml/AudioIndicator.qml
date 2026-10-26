import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Effects

Item {
    id: root
    width: 80
    height: 80

    property string labelText: "AUDIO"
    property string iconSource: ""
    property var recorderObject: null
    property string visualizerMode: "volume"
    property bool visualizerEnabled: true
    property bool isSelected: false      // 新增：选中状态
    property bool isEnabled: true        // 新增：启用状态

    signal clicked()

    property bool isHovered: mouseArea.containsMouse
    property bool isRecording: recorderObject ? recorderObject.isRecording : false

    // 选中状态边框（呼吸框）
    Rectangle {
        id: selectionFrame
        anchors.fill: parent
        radius: 6
        color: "transparent"
        border.color: (isSelected && !isRecording) ? "#4A9EFF" : "transparent"
        border.width: 2
        opacity: isEnabled ? 1.0 : 0.4

        SequentialAnimation {
            running: isSelected && !isRecording && isEnabled
            loops: Animation.Infinite
            OpacityAnimator { target: selectionFrame; from: 1.0; to: 0.5; duration: 800; easing.type: Easing.InOutQuad }
            OpacityAnimator { target: selectionFrame; from: 0.5; to: 1.0; duration: 800; easing.type: Easing.InOutQuad }
        }

        Behavior on border.color {
            ColorAnimation { duration: 200 }
        }
    }

    PanelBackground {
        anchors.fill: parent
        anchors.margins: 2
        isHovered: root.isHovered && root.isEnabled
        isRecording: root.isRecording && root.isSelected
        opacity: root.isEnabled ? 1.0 : 0.5
    }

    Image {
        id: iconImage
        anchors.centerIn: parent
        source: iconSource
        width: 30
        height: 30
        fillMode: Image.PreserveAspectCrop
        visible: !audioVisualizerLoader.active
        opacity: visible ? 1 : 0

        Behavior on opacity {
            NumberAnimation {
                duration: 200
            }
        }
    }

    Loader {
        id: audioVisualizerLoader
        anchors.fill: parent
        anchors.margins: 8
        anchors.topMargin: 10
        anchors.bottomMargin: 12

        active: visualizerEnabled && isRecording && recorderObject && recorderObject.audioSampler
        asynchronous: true
        opacity: active ? 1 : 0

        sourceComponent: Component {
            AudioVisualizer {
                audioSampler: recorderObject ? recorderObject.audioSampler : null
                mode: root.visualizerMode
            }
        }

        Behavior on opacity {
            NumberAnimation {
                duration: 400
                easing.type: Easing.OutCubic
            }
        }
    }

    Text {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 6
        anchors.horizontalCenter: parent.horizontalCenter
        text: labelText
        color: isRecording ? "#4A9EFF" : (isHovered ? "#FFFFFF" : "#B0B0B0")
        font.pixelSize: 10
        font.bold: true
        font.letterSpacing: 0.3

        Behavior on color {
            ColorAnimation {
                duration: 200
            }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.isEnabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor

        onClicked: {
            if (root.isEnabled) {
                root.clicked()
            }
        }
    }

    scale: isHovered ? 1.05 : 1.0
    transformOrigin: Item.Center
    Behavior on scale {
        NumberAnimation {
            duration: 200
            easing.type: Easing.OutCubic
        }
    }
}

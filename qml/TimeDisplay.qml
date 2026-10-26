import QtQuick 2.15
import QtQuick.Effects

Item {
    id: root
    width: 160
    height: 80

    property var recorderObject: null
    property string fallbackText: "00:00:00"
    property color recordingColor: "#4A9EFF"

    readonly property bool isHovered: mouseArea.containsMouse
    readonly property bool isRecording: recorderObject ? recorderObject.isRecording : false

    PanelBackground {
        anchors.fill: parent
        isHovered: root.isHovered
        isRecording: root.isRecording
        recordingColor: root.recordingColor
    }

    property string digitalFontFamily: "Courier New"

    Text {
        anchors.centerIn: parent
        text: "88:88:88"
        font.family: digitalFontFamily
        font.pixelSize: 32
        font.bold: true
        color: "#1Affffff"
    }

    Text {
        id: timeText
        anchors.centerIn: parent
        width: parent.width - 10
        horizontalAlignment: Text.AlignHCenter

        text: (recorderObject && recorderObject.captureTimeText) ? recorderObject.captureTimeText : fallbackText
        color: "#FFFFFF"

        font.family: digitalFontFamily
        font.pixelSize: 32
        font.bold: true
        font.letterSpacing: 2

        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 20

        layer.enabled: isRecording
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: timeText.color
            shadowBlur: 1.0
            shadowVerticalOffset: 0
            shadowHorizontalOffset: 0
            opacity: 0.5
        }

        Behavior on color {
            ColorAnimation { duration: 200 }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }

    scale: isHovered ? 1.05 : 1.0
    transformOrigin: Item.Center
    Behavior on scale {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }
}

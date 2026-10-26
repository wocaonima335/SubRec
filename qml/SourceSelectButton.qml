import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Effects

Rectangle {
    id: root

    property bool isSelected: false
    property bool isEnabled: true
    property string iconSource: ""
    property string labelText: ""
    property real iconWidth: 32
    property real iconHeight: 32

    signal clicked()

    width: 72
    height: parent.height - 12
    anchors.verticalCenter: parent.verticalCenter
    radius: 10

    opacity: isEnabled ? 1.0 : 0.5

    // 渐变背景
    gradient: Gradient {
        GradientStop {
            position: 0.0
            color: isSelected ? "#5BC9FF" : (bgMouse.containsMouse && isEnabled ? "#4A9EFF" : "#3A4050")
        }
        GradientStop {
            position: 1.0
            color: isSelected ? "#38B2F0" : (bgMouse.containsMouse && isEnabled ? "#3A7FCC" : "#2A2F3E")
        }
    }

    border.color: isSelected ? "#7DD8FF" : "transparent"
    border.width: isSelected ? 1 : 0

    Behavior on border.color {
        ColorAnimation { duration: 200 }
    }

    // 阴影效果
    layer.enabled: isSelected || (bgMouse.containsMouse && isEnabled)
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: isSelected ? "#405BC9FF" : "#20000000"
        shadowVerticalOffset: 3
        shadowBlur: 0.8
    }

    MouseArea {
        id: bgMouse
        hoverEnabled: isEnabled
        anchors.fill: parent
        cursorShape: isEnabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
        enabled: isEnabled

        onClicked: {
            if (isEnabled) {
                root.clicked()
            }
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 4

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            source: root.iconSource
            width: root.iconWidth
            height: root.iconHeight
            fillMode: Image.PreserveAspectFit

            // 图片缩放动画
            scale: bgMouse.containsMouse ? 1.1 : 1.0
            Behavior on scale {
                NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.labelText
            color: isSelected ? "#FFFFFF" : "#A0A5B0"
            font.pixelSize: 10
            font.bold: true
            font.letterSpacing: 0.5

            Behavior on color {
                ColorAnimation { duration: 200 }
            }
        }
    }

    // 选中指示器
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 3
        width: 20
        height: 3
        radius: 1.5
        color: "#FFFFFF"
        opacity: isSelected ? 0.8 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation { duration: 200 }
        }
    }
}

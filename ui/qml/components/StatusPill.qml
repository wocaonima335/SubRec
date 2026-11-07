pragma ComponentBehavior: Bound
import QtQuick

// Module U2 (ZCode). §2.5.8 status pill, 24 high, radius 12, 8 px dot, `type/badge` text.
// FROZEN API:
//   uiState: RecorderView.uiState values ("noDevice" | "idle" | "preparing" | "running" |
//            "paused" | "stopping" | "failed") -> colours, dot/spinner, default text
//   text: override the default text (e.g. title-bar mini indicator "录制中 00:12:34")
//   clickable + signal clicked() (title-bar mini indicator jumps to P1)
Rectangle {
    id: root
    property string uiState: "idle"
    property string text: ""
    property bool clickable: false
    signal clicked()

    readonly property string __label: text.length > 0 ? text : defaultLabel
    readonly property string defaultLabel: {
        switch (uiState) {
        case "noDevice": return qsTr("无可用设备")
        case "preparing": return qsTr("准备中…")
        case "running": return qsTr("录制中")
        case "paused": return qsTr("已暂停")
        case "stopping": return qsTr("正在保存…")
        case "failed": return qsTr("出错")
        default: return qsTr("就绪")
        }
    }
    // §2.5.8 table: (bg, fg for dot and text). preparing/stopping use a 12 px spinner.
    readonly property color __bg: uiState === "preparing" || uiState === "stopping" ? Theme.infoBg
                                : uiState === "running" ? Theme.dangerBg
                                : uiState === "paused" ? Theme.warningBg
                                : uiState === "failed" ? Theme.dangerBg
                                : Theme.bgSurface2
    readonly property color __fg: uiState === "preparing" || uiState === "stopping" ? Theme.infoFg
                                : uiState === "running" ? Theme.recordDefault
                                : uiState === "paused" ? Theme.warningFg
                                : uiState === "failed" ? Theme.dangerFg
                                : uiState === "noDevice" ? Theme.textTertiary
                                : Theme.successFg
    readonly property color __textColor: uiState === "running" ? Theme.textPrimary
                                       : uiState === "preparing" || uiState === "stopping" ? Theme.infoFg
                                       : uiState === "paused" ? Theme.warningFg
                                       : uiState === "failed" ? Theme.dangerFg
                                       : Theme.textSecondary
    readonly property bool __spinner: uiState === "preparing" || uiState === "stopping"

    implicitHeight: 24
    implicitWidth: 20 + label.implicitWidth + 10 // 10 px left pad, dot 8+6, 10 px right pad
    radius: 12
    color: __bg

    Loader {
        id: indicator
        x: 10
        anchors.verticalCenter: parent.verticalCenter
        sourceComponent: root.__spinner ? spinnerComp : dotComp

        Component {
            id: dotComp
            Item {
                width: 8
                height: 8
                Rectangle {
                    anchors.centerIn: parent
                    width: 8
                    height: 8
                    radius: 4
                    color: root.__fg

                    // §2.5.8: recording dot blinks at 1 s, 100 % <-> 30 %; steady when
                    // reduce-motion is on (§2.6).
                    SequentialAnimation on opacity {
                        running: root.uiState === "running" && !Theme.reduceMotion
                        loops: Animation.Infinite
                        NumberAnimation { from: 1.0; to: 0.3; duration: Theme.blinkPeriod / 2; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 0.3; to: 1.0; duration: Theme.blinkPeriod / 2; easing.type: Easing.InOutSine }
                    }
                }
            }
        }
        Component {
            id: spinnerComp
            Spinner {
                size: 12
                color: root.__fg
                running: true
            }
        }
    }

    Text {
        id: label
        anchors.left: indicator.right
        anchors.leftMargin: Theme.space8 - 2
        anchors.verticalCenter: parent.verticalCenter
        text: root.__label
        font: Theme.badgeFont
        color: root.__textColor
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.clickable
        cursorShape: root.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.clicked()
    }
}
